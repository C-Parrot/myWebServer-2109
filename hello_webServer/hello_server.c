#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024
void error_handling(char *message);

int main(int argc, char * argv[]){
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    char message[BUF_SIZE];

    if(argc!=2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}

    //创建套接字
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);//PF: protocol family.协议族-类型-最终协议，PF_INET:ipv4协议族
    if(serv_sock == -1)
        error_handling("socket() error");
    
    //初始化结构体变量
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;//地址族，AF: address family，ipv4网络协议中使用的地址族
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//h表示主机，n表示network
    serv_addr.sin_port = htons(atoi(argv[1]));
    
    //套接字地址分配
    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    //接收端进入等待连接请求状态
    //第二个参数为等待队列大小，频繁接收请求的Web服务器端至少应为15。另外，连接请求队列的大小始终根据实验结果而定
    if(listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    
    clnt_addr_size = sizeof(clnt_addr);

    for(int i=0; i<5; ++i){
        //accept会从半连接队列中取1个连接请求，若等待队列为空，则accept函数不会返回
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock == -1) error_handling("accept() error");
        else printf("Connected client %d \n", i+1);
        int str_len;
        while((str_len = read(clnt_sock, message, BUF_SIZE)) != 0){
            write(clnt_sock, message, str_len);//将收到的数据再返回客户端
        }
        close(clnt_sock);
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