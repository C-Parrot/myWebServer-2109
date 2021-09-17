#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define BUF_SIZE 1024
void error_handling(char *message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}
void read_childproc(int sig){
	int status;
    int pid = waitpid(-1, &status, WNOHANG);
    printf("removed proc id：%d\n", pid);
}

int main(int argc, char *argv[]){

    //信号注册，子进程退出时，会自动调用read_childproc
    struct sigaction act;
    act.sa_handler = read_childproc;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    int state = sigaction(SIGCHLD, &act, 0);

    int serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr, clnt_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if(listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    
    int fds[2];
    pipe(fds);
    int pid = fork();
    if(pid == 0){//通过管道将获得的数据存在本地文件中
        FILE *fp = fopen("echomsg.txt", "wt");
        char msgbuf[BUF_SIZE];
        int i, len;
        //由于服务会一直运行，这里设置向文件写入一定次数之后自动关闭
        for(int i=0; i<10; ++i){
            len = read(fds[0], msgbuf, BUF_SIZE);
            fwrite((void*)msgbuf, 1, len, fp);
        }
        fclose(fp);
        return 0;
    }

    char buf[BUF_SIZE];
    int str_len;

    while(1){
        int adr_sz = sizeof(clnt_addr);
        int clnt_sock = accept(serv_sock, (struct sockaddr*) &clnt_addr, &adr_sz);
        if(clnt_sock == -1) continue;
        else puts("new clent connected...");
        pid = fork();//生成子进程，负责管理连接
        if(pid == -1){
            close(clnt_sock);
            continue;
        }
        else if(pid == 0){
            close(serv_sock);//调用fork后，将无关的套接字文件描述符关掉
            while( ( str_len= read(clnt_sock, buf, BUF_SIZE)) != 0){
                write(clnt_sock, buf, str_len);
                write(fds[1], buf, str_len);//向管道写入获得的信息
            }
            close(clnt_sock);
            puts("client disconnected...");
            return 0;
        }
        else close(clnt_sock);//只有某个套接字的所有文件描述符都关掉后，才会销毁套接字。
    }
    close(serv_sock);
    return 0;
}

