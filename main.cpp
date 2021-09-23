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
#include "threadpool.h"
#include "http_conn.h"
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
void error_handling(const char *message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    threadpool<http_conn> *pool = nullptr;
    try{
        pool = new threadpool<http_conn>;
    }
    catch(...){
        return 1;
    }


    int serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse) );//so_resuseaddr

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if(listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    
    int epfd = epoll_create(100);//内核会忽略参数，根据实际情况调整epoll例程的大小。
    epoll_event events[MAX_EVENT_NUMBER];

    epoll_event event;
    //EPOLLRDHUP，如果对方异常断开，内核负责处理。如果在应用层，则需通过read的返回值来决定做相应处理
    event.events = EPOLLIN | EPOLLRDHUP;//本项目使用的是epolllt,好的服务器应该既支持LT也支持ET，可以增加配置文件去决定使用哪个
        
    event.data.fd = serv_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);//向epfd中注册serv_sock，监听EPOLLIN(可读)。CTL：control
    int flag = fcntl(serv_sock, F_GETFL, 0);//设置非阻塞
    fcntl(serv_sock, F_SETFL, flag | O_NONBLOCK);
    http_conn::m_epollfd = epfd;

    http_conn* users = new http_conn[MAX_FD];
    while(true){
        int event_cnt = epoll_wait(epfd, events, MAX_EVENT_NUMBER, -1);//-1:阻塞直到发生事件
        if(event_cnt <0 && errno != EINTR){ // EINTR:被中断
            printf("epoll_wait() error");
            break;
        }
        for(int i=0; i<event_cnt; i++){
            int cur_fd = events[i].data.fd;
            if(cur_fd == serv_sock){
                struct sockaddr_in clnt_addr;
                socklen_t adr_sz = sizeof(clnt_addr);
                int clnt_sock = accept(serv_sock, (struct sockaddr*) &clnt_addr, &adr_sz);
                if ( clnt_sock < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 
                if( http_conn::m_user_count >= MAX_FD ) {//fd已用完？
                    //此处可以给客户端返回503：服务器正忙，无法处理请求
                    close(clnt_sock);
                    continue;
                }
                users[clnt_sock].init( clnt_sock, clnt_addr);
            }
            else if( events[i].events & EPOLLRDHUP) {//如果对端异常断开
                users[cur_fd].close_conn();//调用close socket关闭连接
            }
            else if(events[i].events & EPOLLIN){//如果是可读事件，一次性将数据全部读出
                if(users[cur_fd].read()) {//如果有数据要处理，主线程负责读取数据
                    pool->push(users + cur_fd);//将任务添加到任务队列中，工作线程负责处理数据
                } 
            }
            else if( events[i].events & EPOLLOUT ) {//如果是写事件
                if( !users[cur_fd].write() ) {
                    users[cur_fd].close_conn();
                }
            }
        }
    }  
    close(serv_sock);
    close(epfd);
    delete[] users;
    delete pool;
    return 0;
}

