#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUF_SIZE 1024
#define MAX_CLNT 256
void error_handling(char *message);


pthread_mutex_t mutx;
int clnt_cnt;//连接套接字数量
int clnt_socks[MAX_CLNT];//存放连接套接字

//工作线程处理函数，将收到的消息转发给所有的客户端
void* handle_clnt(void* arg){
    int clnt_sock = *((int*)arg);
    int str_len = 0, i;
    char msg[BUF_SIZE];
    while((str_len = read(clnt_sock, msg, BUF_SIZE)) != 0){
        pthread_mutex_lock(&mutx);//互斥量，保证下面循环时，clnt_socks不会被其它线程改变
        for(int i=0; i<clnt_cnt; ++i){//向所有客户端发送消息
            write(clnt_socks[i], msg, str_len);
        }
        pthread_mutex_unlock(&mutx);
    }
    pthread_mutex_lock(&mutx);
    //read为0循环结束，即收到FIN
    for(int i=0; i<clnt_cnt; ++i){//从连接套接字数组中移除本套接字
        if(clnt_sock == clnt_socks[i]){
            while(i++<clnt_cnt-1)
                clnt_socks[i] = clnt_socks[i+1];
            break;
        }
    }
    clnt_cnt--;
    pthread_mutex_unlock(&mutx);
    close(clnt_sock);//关闭套接字
    return NULL;
}

int main(int argc, char * argv[]){
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    char message[BUF_SIZE];

    if(argc!=2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
    pthread_mutex_init(&mutx, NULL);
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);//PF: protocol family.协议族-类型-最终协议，PF_INET:ipv4协议族
    if(serv_sock == -1)
        error_handling("socket() error");
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;//地址族，AF: address family，ipv4网络协议中使用的地址族
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//h表示主机，n表示network
    serv_addr.sin_port = htons(atoi(argv[1]));
    
    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if(listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    
    clnt_addr_size = sizeof(clnt_addr);
    while(1){
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock == -1) error_handling("accept() error");
        pthread_mutex_lock(&mutx);
        clnt_socks[clnt_cnt++] = clnt_sock;
        pthread_mutex_unlock(&mutx);
        pthread_t t;
        pthread_create(&t, NULL, handle_clnt, (void*)&clnt_sock);
        pthread_detach(t);
        printf("Connected client IP: %s \n", inet_ntoa(clnt_addr.sin_addr));
    }
    close(serv_sock);
    return 0;
}
void error_handling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}