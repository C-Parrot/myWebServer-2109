#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024

int main(int argc, char * argv[]){
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    char message[BUF_SIZE];

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);//protocol family.协议族-类型-最终协议，PF_INET:ipv4协议族
    if(serv_sock == -1)
        error_handing("socket() error");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;//地址族，address family，基于ipv4的地址族,ipv4网络协议中使用的地址族
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//h表示主机，n表示network
    serv_addr.sin_port = hton(atoi(argv[1]));
    
    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handing("bind() error");
    //第二个参数为等待队列大小，频繁接收请求的Web服务器端至少应为15。另外，连接请求队列的大小始终根据实验结果而定
    if(listen(serv_sock, 5) == -1)
        error_handing("listen() error");
    
    clnt_addr_size = sizeof(clnt_addr);

    for(int i=0; i<5; ++i){
        //accept会从半连接队列中取1个连接请求，若等待队列为空，则accept函数不会返回
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock == -1) error_handling("accept() error");
        else printf("Connected client ");
        int str_len;
        while((str_len = read(clnt_sock, message, BUF_SIZE)) != 0){
            write(clnt_sock, message, str_len);
        }
        close(clnt_sock);
    }


    close(serv_sock);
    return 0;

}
/*
clnt_sd=accept(serv_sd, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
	
	while(1)
	{
		read_cnt=fread((void*)buf, 1, BUF_SIZE, fp);
		if(read_cnt<BUF_SIZE)
		{
			write(clnt_sd, buf, read_cnt);
			break;
		}
		write(clnt_sd, buf, BUF_SIZE);
	}
	
	shutdown(clnt_sd, SHUT_WR);	
	read(clnt_sd, buf, BUF_SIZE);
	printf("Message from client: %s \n", buf);
	
	fclose(fp);
	close(clnt_sd); close(serv_sd);
	return 0;

    */