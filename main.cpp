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
#include <assert.h>

#include "threadpool.h"
#include "http_conn.h"
#include "log.h"
#include "timer.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 30  

int pipefd[2];

void error_handling(const char *message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

//设置信号
void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1 );
}

//信号SIGALRM的处理函数
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0 );
    errno = save_errno;
}

int main(int argc, char *argv[]){

    Log::get_instance()->init("./ServerLog", 0, 2000, 800000, 0);//初始化日志

    if(argc != 2){
        printf("Usage: %s <port>\n", argv[0]);
        LOG_ERROR("%s", "epoll failure");
        return 1;
    }

    addsig( SIGPIPE, SIG_IGN );//SIGPIPE信号的默认执行动作是terminate(终止、退出),对于服务器，将SIGPIPE执行函数设为SIG_IGN

    threadpool<http_conn> *pool = nullptr;
    try{
        pool = new threadpool<http_conn>;
    }
    catch(...){
        LOG_ERROR("%s", "create threadpoll failure");
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
    // 创建管道
    sort_timer_lst timer_lst;//定时器链表（增序双向）
    if(socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd) == -1)
        error_handling("socketpair() error");
    flag = fcntl( pipefd[1], F_GETFL );
    fcntl( pipefd[1], F_SETFL, flag | O_NONBLOCK );
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = pipefd[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &event);

    // 设置信号处理函数
    addsig(SIGALRM, sig_handler);
    bool stop_server = false;
    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

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

                char ip[16] = {0};
                inet_ntop(AF_INET, &clnt_addr.sin_addr ,ip, sizeof(ip));
                LOG_INFO("client(%s) is connected", ip);

                users[clnt_sock].init(clnt_sock, clnt_addr, TIMESLOT);
                timer_lst.add_timer( users[clnt_sock].timer);
            }
            else if(cur_fd == pipefd[0] && (events[i].events & EPOLLIN)){
                //探活
                int sig;
                char signals[1024];
                int ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if(ret == -1) continue;
                else if(ret == 0) continue;
                else timeout = true;
            }
            else if( events[i].events & EPOLLRDHUP) {//如果对端异常断开
                util_timer* timer = users[cur_fd].timer;
                users[cur_fd].close_conn();//调用close socket关闭连接
                timer_lst.del_timer(timer);//删除对应的计时器

            }
            else if(events[i].events & EPOLLIN){//如果是可读事件，一次性将数据全部读出
                util_timer* timer = users[cur_fd].timer;
                if(users[cur_fd].read()) {//如果有数据要处理，主线程负责读取数据
                    pool->push(users + cur_fd);//将任务添加到任务队列中，工作线程负责处理数据
                    time_t cur = time( NULL );
                    timer->expire = cur + 3 * TIMESLOT;
                    printf( "adjust timer once\n" );
                    timer_lst.adjust_timer(timer);//重置定时器
                }
                else{//如果读取发生错误或连接异常,从队列中移除对应的计时器
                    timer_lst.del_timer(timer);
                } 
            }
            else if( events[i].events & EPOLLOUT ) {//如果是写事件
                if( !users[cur_fd].write() ) {
                    util_timer* timer = users[cur_fd].timer;
                    users[cur_fd].close_conn();//close_conn()会将users[cur_fd].timer置为nullptr
                    timer_lst.del_timer(timer);
                }
            }
            
        }
        // 先将所有I/O事件处理完，最后处理定时事件，这样做将导致定时任务不能精准的按照预定的时间执行。
        if(timeout) {
            timer_lst.tick();//如果超时，会去调用用户类中的close_conn()
            alarm(TIMESLOT);// 一次 alarm 调用只会引起一次SIGALARM 信号，要重新定时，以不断触发 SIGALARM信号。
            timeout = false;
        }
    }  
    close(serv_sock);
    close(epfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete pool;
    return 0;
}

