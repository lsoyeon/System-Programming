/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

void echo(int connfd);
void handle_sigint(int sig);

int client_count;
/* stcok(주식 구매와 판매를 구현하기 위한 구조체와 함수들)*/
typedef struct stock_item{
    int ID, price, cnt;
    struct stock_item* left, *right;
} stock_item;
stock_item* root;

/* 주식 한 item을 binary tree에 추가*/
stock_item* add_item(stock_item* cur, stock_item* pt){
    if(!cur) return pt;
    if(pt->ID == cur->ID) cur->cnt += pt->cnt;
    else if(pt->ID < cur->ID) cur->left = add_item(cur->left, pt);
    else cur->right = add_item(cur->right, pt);
    return cur;
}

/* 주식 상태를 출력해서 buf에 붙여주는 함수*/
int stock_show(stock_item* cur, char* pt){
    if(!cur) return 0;
    int n = stock_show(cur->left, pt);
    n += sprintf(pt + n, "%d %d %d\n", cur->ID, cur->cnt, cur->price);
    n += stock_show(cur->right, pt + n);
    return n;
}

/* stock.txt에 있는 주식 데이터를 binary tree에 저장하는 함수*/
void stock_load(){
    FILE* fp = fopen("stock.txt", "r");
    int id, price, cnt;
    while(fscanf(fp, "%d %d %d", &id, &cnt, &price) != EOF){
        stock_item* pt = (stock_item*) malloc(sizeof(stock_item));
        pt->ID = id;
        pt->price = price;
        pt->cnt = cnt;
        pt->left = NULL; pt->right = NULL;
        root = add_item(root, pt);
    }
    fclose(fp);
    return;
}

/* binary tree에 있는 주식 정보를 stock.txt에 저장하는 함수*/
void stock_save(){
    FILE* fp = fopen("stock.txt", "w");
    char buf[MAXLINE] = {'\0'};
    stock_show(root, buf);
    fprintf(fp, "%s", buf);
    fclose(fp);
    return;
}

/* 주식 개수를 확인하고 변경해주는 함수*/
int stock_update(stock_item* cur, int id, int cnt){
    if(!cur) return 0;
    if(id == cur->ID){
        if(cur->cnt + cnt < 0) return 0;
        cur->cnt += cnt;
        return 1;
    }
    else if(id < cur->ID) return stock_update(cur->left, id, cnt);
    else return stock_update(cur->right, id, cnt);
}

/* connected된 descriptor를 저장하기 위한 구조체(pool)*/
typedef struct{
    int maxfd;
    fd_set read_set, ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];   // FD_SETSIZE = 1024
    rio_t clientrio[FD_SETSIZE];
} Pool;

/* initialize pool */
void init_pool(int listenfd, Pool* p){
    /* initialaize */
    p->maxi = -1;
    for(int i = 0; i < FD_SETSIZE; i++) p->clientfd[i] = -1;
    FD_ZERO(&p->read_set);

    /* listenfd를 pool에 추가*/
    p->maxfd = listenfd;
    FD_SET(listenfd, &p->read_set);
    return;
}

/* add client file descriptor to pool*/
void add_client(int connfd, Pool* p){
    p->nready--;
    for(int i = 0; i < FD_SETSIZE; i++)
        /* i번째 slot이 비어있으면 거기에 connected descripotr를 추가*/
        if(p->clientfd[i] < 0){
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);
            client_count++;

            // 이제부터 connfd로도 input을 받음
            FD_SET(connfd, &p->read_set);
            if(connfd > p->maxfd) p->maxfd = connfd;
            if(i > p->maxi) p->maxi = i;
            return;
        }
    // fail to find available slot
    app_error("add_client error: Too many clients");
    return;
}

void eval(char* buf, char* ret){
    char cmd[MAXLINE];
    int id, cnt;
    sscanf(buf, "%s", cmd);
    /* show */
    if(!strcmp(cmd, "show")) stock_show(root, ret);
    /* buy [ID] [cnt]*/
    else if(!strcmp(cmd, "buy")){
        sscanf(buf+3, "%d %d", &id, &cnt);
        if(stock_update(root, id, -cnt)) strcpy(ret, "[buy] success\n");
        else strcpy(ret, "Not enough left stocks\n");
    }
    /* sell [ID] [cnt]*/
    else if(!strcmp(cmd, "sell")){
        sscanf(buf+4, "%d %d", &id, &cnt);
        if(stock_update(root, id, cnt)) strcpy(ret, "[sell] success\n");
        else strcpy(ret, "Stock does not exist\n");
    }
    /* exit */
    else if(!strcmp(cmd, "exit")) strcpy(ret, "exit");
    return;
}

int byte_cnt = 0; // server가 받음 total byte
/* ready된 descriptor에 대해 하나의 textline을 echo*/
void check_clients(Pool* p){
    char buf[MAXLINE];
    for(int i = 0; i <= p->maxi && p->nready > 0; i++){
        int connfd = p->clientfd[i];
        rio_t rio = p->clientrio[i];

        /* descriptor가 존재하고 pending input이 있는 경우(ready)*/
        if(connfd > 0 && FD_ISSET(connfd, &p->ready_set)){
            p->nready--;
            char ret[MAXLINE] = {'\0'};
            /* 한 줄씩 input을 받음*/
            int n = Rio_readlineb(&rio, buf, MAXLINE);
            if(n){
                byte_cnt += n;
                printf("server received %d bytes\n", n);
                eval(buf, ret);
            } 

            /* EOF를 입력으로 받은 경우 connfd를 close*/
            if(!n || !strcmp(ret, "exit")){
                Close(connfd); client_count--;
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }
            /* 아니면 client에게 결과 출력*/
            else Rio_writen(connfd, ret, MAXLINE); 
        }
    }
    return;
}

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    // char client_hostname[MAXLINE], client_port[MAXLINE];
    static Pool pool;

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    Signal(SIGINT, handle_sigint);

    listenfd = Open_listenfd(argv[1]);
    init_pool(listenfd, &pool);
    stock_load();
    
    /* 기존에 주어진 뼈대 코드
    while (1) {
	    clientlen = sizeof(struct sockaddr_storage); 
	    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
	    echo(connfd);
	    Close(connfd);
    }
    */
    while(1){
        pool.ready_set = pool.read_set;
        /* select를 통해 pending input이 있는 descriptor를 뽑음*/
        pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);
        /* listen file descriptor가 ready 상태면 새로운 client를 pool에 추가*/
        if(FD_ISSET(listenfd, &pool.ready_set)){
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA*) &clientaddr, &clientlen);
            add_client(connfd, &pool);
        }
        /* ready된 descriptor에 대해 하나의 textline을 echo*/
        check_clients(&pool);

        /* 연결된 client가 없으면 서버 종료*/
        // if(!client_count){
        //     stock_save(); 
        //     break;
        // }
    }
    exit(0);
}

void handle_sigint(int sig){
    stock_save();
    exit(0);
    return;
}
/* $end echoserverimain */