/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

void echo(int connfd);
void handle_sigint(int sig);
void echo_cnt(int connfd);

#define SBUFSIZE 105
#define NTHREADS 105

static int byte_cnt = 0; // server가 받음 total byte
static sem_t mutex;
/* 책(강의자료)에 있는 sbuf 구현체와 함수들 */
typedef struct {
    int *buf; /* Buffer array */
    int n; /* Maximum number of slots */
    int front; /* buf[(front+1)%n] is the first item */
    int rear; /* buf[rear%n] is the last item */
    sem_t mutex; /* Protects accesses to buf */
    sem_t slots; /* Counts available slots */
    sem_t items; /* Counts available items */
} sbuf_t;
sbuf_t sbuf;
/* Create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_init(sbuf_t *sp, int n){
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n; /* Buffer holds max of n items */
    sp->front = sp->rear = 0; /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0); /* Initially, buf has 0 items */
}
/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp){
    Free(sp->buf);
}
/* Insert an item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, int item){
    P(&sp->slots); /* Wait for available slot */
    P(&sp->mutex); /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item; /* Insert the item */
    V(&sp->mutex); /* Unlock the buffer */
    V(&sp->items); /* Announce available item */
}
/* Remove and return the first item from buffer sp */
int sbuf_remove(sbuf_t *sp){
    int item;
    P(&sp->items); /* Wait for available item */
    P(&sp->mutex); /* Lock the buffer */
    item = sp->buf[(++sp->front)%(sp->n)]; /* Remove the item */
    V(&sp->mutex); /* Unlock the buffer */
    V(&sp->slots); /* Announce available slot */
    return item;
}
void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while(1) {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buf */
        echo_cnt(connfd); /* Service client */
        Close(connfd);
    }
}
static void init_echo_cnt(void){
    Sem_init(&mutex, 0, 1);
    byte_cnt = 0;
    return;
}
/*end*/

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

/*각 thread에서 하나의 client를 관리할 때 사용하는 함수*/
void echo_cnt(int connfd){
    char buf[MAXLINE], ret[MAXLINE] = {'\0'};;
    rio_t rio;
    static pthread_once_t once_p = PTHREAD_ONCE_INIT;

    Pthread_once(&once_p, init_echo_cnt);
    Rio_readinitb(&rio, connfd);    
    int n;
    /* client로부터 입력이 들어온 경우*/
    while((n = Rio_readlineb(&rio, buf, MAXLINE))){
        /* thread safe하도록 구현하기 위해 */
        P(&mutex);
        byte_cnt += n;
        printf("server received %d bytes\n", n);
        eval(buf, ret);

        /* exit을 입력받은 경우 종료*/
        if(!strcmp(ret, "exit")) break;
        /* 아니면 client에게 결과 출력*/
        else Rio_writen(connfd, ret, MAXLINE); 
        V(&mutex);
    }
    /* EOF가 입력되면 while문을 빠져나옴.*/
    return;
}

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    // char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    Signal(SIGINT, handle_sigint);

    stock_load();
    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; i++ ) /* Create a pool of worker threads */
        Pthread_create(&tid, NULL, thread, NULL); 
    
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
        /* 새로운 client가 connect될 때 처리*/
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA*) &clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);
    }
    exit(0);
}

void handle_sigint(int sig){
    stock_save();
    exit(0);
    return;
}
/* $end echoserverimain */