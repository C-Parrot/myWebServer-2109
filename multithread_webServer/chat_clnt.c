#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#define BUF_SIZE 100
#define NAME_SIZE 20
void error_handling(char *message);
char message[BUF_SIZE], name[NAME_SIZE] = "[DEFAULT]";

void* send_msg(void* arg){
    int sock = *((int*)arg);
    char name_msg[NAME_SIZE+BUF_SIZE];
    while(1){
        fgets(message, BUF_SIZE, stdin);
        if(!strcmp(message, "q\n") || !strcmp(message, "Q\n")){
            close(sock);
            exit(0);
        } 
        sprintf(name_msg,"%s %s", name, message);
		write(sock, name_msg, strlen(name_msg));
    }   
    return NULL;
}

void* rcv_msg(void* arg){
    int sock=*((int*)arg);
	char name_msg[NAME_SIZE+BUF_SIZE];
	while(1)
	{
		int str_len=read(sock, name_msg, NAME_SIZE+BUF_SIZE-1);
		if(str_len==-1) 
			return (void*)-1;
		name_msg[str_len]=0;
		fputs(name_msg, stdout);
	}
	return NULL;
}

int main(int argc, char* argv[]){
    int sock;
    struct sockaddr_in serv_addr;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    char message[30];

    if(argc!= 4) {
		printf("Usage : %s <IP> <port> <name>\n", argv[0]);
		exit(1);
	}
    
    //用于连接服务端的套接字
    sock=socket(PF_INET, SOCK_STREAM, 0);   
	if(sock==-1)
		error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    //向服务端连接请求，建立连接
    if(connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error!");
    else
		puts("Connected...........");

    pthread_t snd_thread, rcv_thread;
    pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);
    pthread_create(&rcv_thread, NULL, rcv_msg, (void*)&sock);
    void* thread_return;
    pthread_join(snd_thread, &thread_return);
    pthread_join(rcv_thread, &thread_return);

    close(sock);
    return 0;
}

void error_handling(char* message){
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}












