#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#define BUF_SIZE 1024

void error_handling(char *message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

int main(int argc, char *argv[]){

    if(argc != 3){
        printf("Usage: %s <IP> <port> \n", argv[0]);
        exit(1);
    }
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_adr;
    memset(&serv_adr, 0, sizeof(serv_adr));//<string.h>
    serv_adr.sin_family = AF_INET;
    if(!inet_aton(argv[1], &serv_adr.sin_addr))
        error_handling("Conversion error");
    serv_adr.sin_port = htons(atoi(argv[2]));

    if(connect(sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("connect() error!");

    int pid = fork();
    char message[BUF_SIZE];

    if(pid == 0){
        while(1){
            //因为输入输出是分割的，下面这句可能在输出之前输出，造成混乱
            //fputs("Input message(Q to quit):" ,stdout);
            fgets(message, BUF_SIZE, stdin);
            if(!strcmp(message, "q\n") || !strcmp(message, "Q\n")){
                shutdown(sock, SHUT_WR);
                break;//如果输入q或Q即退出子进程,调用下方的close
            } 
            write(sock, message, strlen(message));
        }
    }
    else{
        while(1){
            int str_len = read(sock, message, BUF_SIZE);
            if(str_len == 0) break;//如果读取为0，退出父进程,调用下方的close
            message[str_len] = 0;
            printf("Message from server:%s", message);
        }
    }
    close(sock);
	return 0;
}
