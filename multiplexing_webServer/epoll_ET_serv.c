#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#define BUF_SIZE 4
#define EPOLL_SIZE 50
void error_handling(char *message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Usage: %s <port>\n", argv[0]);
    }

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
    
    int flag = fcntl(serv_sock, F_GETFL, 0);//边缘模式，要将套接字设置为非阻塞的，read将为非阻塞的
    fcntl(serv_sock, F_SETFL, flag|O_NONBLOCK);

    int epfd = epoll_create(EPOLL_SIZE);
    struct epoll_event *ep_events;
    ep_events = malloc(sizeof(struct epoll_event) *EPOLL_SIZE);
    
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = serv_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);

    while(1){
        int event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
        if(event_cnt == -1){
            puts("epoll_wait() error");
            break;
        }
        puts("return epoll_wait");
        for(int i=0; i<event_cnt; i++){
            int cur_fd = ep_events[i].data.fd;
            if(cur_fd == serv_sock){
                int adr_sz = sizeof(clnt_addr);
                int clnt_sock = accept(serv_sock, (struct sockaddr*) &clnt_addr, &adr_sz);
                int flag = fcntl(clnt_sock, F_GETFL, 0);//设置非阻塞
                fcntl(clnt_sock, F_SETFL, flag|O_NONBLOCK);
                event.events = EPOLLIN;
                event.data.fd = clnt_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sock, &event);
            }
            else{
                char buf[BUF_SIZE];
                while(1){//边缘模式下，⼀旦发⽣输⼊相关事件时，就应该读取输⼊缓冲中的全部数据
                    int str_len = read(cur_fd, buf, BUF_SIZE);
                    if(str_len == 0){//FIN
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL);
                        close(cur_fd);
                        printf("close client: %d \n", cur_fd);
                    }
                    else if(str_len<0){//没有数据可读时，read返回1，errno值为EAGAIN
                        if(errno==EAGAIN){
                            break;
                        }
                    }
                    else{
                        write(cur_fd, buf, str_len);//回声
                    }
                }
                
            }
        }
    }
    close(serv_sock);
    close(epfd);
    return 0;
}

