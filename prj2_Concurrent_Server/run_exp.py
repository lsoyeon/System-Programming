import os
import subprocess
import time
import re

def run_cmd(cmd, cwd=None):
    # print(f"Running: {cmd}")
    res = subprocess.run(f"wsl bash -c '{cmd}'", shell=True, cwd=cwd, capture_output=True, text=True, encoding='utf-8')
    if res.returncode != 0:
        print(f"Error ({res.returncode}): {res.stderr}\n{res.stdout}")
    return res.stdout

def setup_client():
    run_cmd("rm -rf task_1_exp task_2_exp task_3_exp")
    run_cmd("cp -r task_1 task_1_exp")
    run_cmd("cp -r task_2 task_2_exp")
    run_cmd("cp -r task_2 task_3_exp")
    
    # 1. Patch multiclient.c in all tasks
    for folder in ['task_1_exp', 'task_2_exp', 'task_3_exp']:
        path = f"{folder}/multiclient.c"
        with open(path, 'r', encoding='utf-8') as f:
            code = f.read()
        if "<time.h>" not in code:
            code = code.replace('#include "csapp.h"', '#include "csapp.h"\n#include <time.h>')
            
        code = code.replace("usleep(1000000);", "// usleep removed", 1)
        
        if "struct timespec start, end;" not in code:
            new_call = """
                struct timespec start, end;
                clock_gettime(CLOCK_MONOTONIC, &start);
                Rio_writen(clientfd, buf, strlen(buf));
            """
            code = code.replace("Rio_writen(clientfd, buf, strlen(buf));", new_call, 1)
            
        if "clock_gettime(CLOCK_MONOTONIC, &end);" not in code:
            new_call = """
                Rio_readnb(&rio, buf, MAXLINE);
                clock_gettime(CLOCK_MONOTONIC, &end);
                long ns = (end.tv_sec - start.tv_sec)*1000000000L + (end.tv_nsec - start.tv_nsec);
                printf("[LATENCY] %ld\\n", ns);
            """
            code = code.replace("Rio_readnb(&rio, buf, MAXLINE);", new_call, 1)
            
        code = code.replace("Fputs(buf, stdout);", "// Fputs removed", 1)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(code)
        run_cmd("make", cwd=folder)

    # 2. Patch stockserver.c in task_3_exp to use RW-Lock
    with open("task_3_exp/stockserver.c", "r", encoding='utf-8') as f:
        sv = f.read()
    
    # Change struct for RW lock
    sv = sv.replace("int ID, price, cnt;", "int ID, price, cnt;\n    sem_t mutex, w_mutex;\n    int readcnt;")
    sv = sv.replace("pt->left = NULL;", "pt->readcnt = 0; Sem_init(&pt->mutex, 0, 1); Sem_init(&pt->w_mutex, 0, 1);\n        pt->left = NULL;")
    
    # Remove global mutex logic from echo_cnt
    sv = sv.replace("P(&mutex);", "")
    sv = sv.replace("V(&mutex);", "")
    
    # In stock_show (Readers)
    show_original = "    if(!cur) return 0;\n    int n = stock_show(cur->left, pt);\n    n += sprintf(pt + n, \"%d %d %d\\n\", cur->ID, cur->cnt, cur->price);\n    n += stock_show(cur->right, pt + n);\n    return n;"
    
    show_replacement = """    if(!cur) return 0;
    int n = stock_show(cur->left, pt);
    P(&cur->mutex);
    cur->readcnt++;
    if(cur->readcnt == 1) P(&cur->w_mutex);
    V(&cur->mutex);
    n += sprintf(pt + n, "%d %d %d\\n", cur->ID, cur->cnt, cur->price);
    P(&cur->mutex);
    cur->readcnt--;
    if(cur->readcnt == 0) V(&cur->w_mutex);
    V(&cur->mutex);
    n += stock_show(cur->right, pt + n);
    return n;"""
    sv = sv.replace(show_original, show_replacement)
    
    # In stock_update (Writers)
    update_original = "    if(id == cur->ID){\n        if(cur->cnt + cnt < 0) return 0;\n        cur->cnt += cnt;\n        return 1;\n    }"
    update_replacement = """    if(id == cur->ID){
        P(&cur->w_mutex);
        if(cur->cnt + cnt < 0) {
            V(&cur->w_mutex);
            return 0;
        }
        cur->cnt += cnt;
        V(&cur->w_mutex);
        return 1;
    }"""
    sv = sv.replace(update_original, update_replacement)
    
    with open("task_3_exp/stockserver.c", "w", encoding='utf-8') as f:
        f.write(sv)
    run_cmd("make", cwd="task_3_exp")

def run_experiment(server_dir, port, num_clients):
    print(f"\\n--- Running {server_dir} ---")
    server_proc = subprocess.Popen(f"wsl bash -c './stockserver {port}'", shell=True, cwd=server_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
    time.sleep(1) 
    
    start_t = time.time()
    client_out = run_cmd(f"./multiclient localhost {port} {num_clients}", cwd=server_dir)
    end_t = time.time()
    
    server_proc.terminate()
    server_proc.kill()
    run_cmd(f"killall stockserver", cwd=server_dir) 
    
    latencies = []
    for line in client_out.splitlines():
        m = re.match(r'^\[LATENCY\] (\d+)', line.strip())
        if m:
            latencies.append(int(m.group(1)) / 1000.0)
            
    if not latencies:
        print("    No latencies found!")
        return None
        
    latencies.sort()
    
    def calc_percentile(data, p):
        k = (len(data) - 1) * p
        f = int(k)
        c = f + 1
        if f == c or c >= len(data):
            return data[f]
        return data[f] + (k - f) * (data[c] - data[f])
    
    p50 = calc_percentile(latencies, 0.50)
    p95 = calc_percentile(latencies, 0.95)
    p99 = calc_percentile(latencies, 0.99)
    avg = sum(latencies) / len(latencies)
    throughput = len(latencies) / (end_t - start_t)
    
    print(f"    Avg Latency:  {avg:7.2f} us")
    print(f"    p50 Latency:  {p50:7.2f} us")
    print(f"    p99 Latency:  {p99:7.2f} us")
    print(f"    Throughput:   {throughput:7.2f} req/s")
    return avg, p50, p99, throughput

if __name__ == "__main__":
    setup_client()
    print("\\n[1] P99 Latency comparison (50 clients) - Mixed Workload")
    run_experiment("task_1_exp", 8121, 50)
    run_experiment("task_2_exp", 8122, 50)
    run_experiment("task_3_exp", 8123, 50)
