/*


*/
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"
using namespace std;

// 线程池类，T是任务类
template<typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool push(T* request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;  // 线程的数量
    pthread_t * m_threads;// 描述线程池的数组，大小为m_thread_number    
    int m_max_requests; // 请求队列中最多允许的、等待处理的请求的数量  
    queue< T* > m_workqueue;  // 请求队列
    locker m_queuelocker;   // 保护请求队列的互斥锁
    sem m_queuestat;// 是否有任务需要处理，默认初始化，初始值为0，其值大小代表了请求队列中的任务个数
    bool m_stop; // 是否结束线程                   
};

template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) : 
        m_thread_number(thread_number), m_max_requests(max_requests), 
        m_stop(false), m_threads(NULL) {

    if((thread_number <= 0) || (max_requests <= 0) ) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程。
    for ( int i = 0; i < thread_number; ++i ) {
        printf( "create the %dth thread\n", i);
        //在c++程序中使用pthread_creat时，该函数的第3个参数必须指向一个静态函数”
        if(pthread_create(m_threads + i, NULL, worker, this ) != 0) {//将此对象传进线程，每个线程共享同一个对象
            delete [] m_threads;
            throw std::exception();
        }
        //线程分离 
        if( pthread_detach( m_threads[i] ) ) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template< typename T >
threadpool< T >::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

//向任务队列中添加任务
template< typename T >
bool threadpool< T >::push( T* request )
{
    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    queue<int> q;
    if ( m_workqueue.size() >= m_max_requests ) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push(request);
    m_queuelocker.unlock();
    m_queuestat.post();//添加了任务，信号量+1
    return true;
}

template< typename T >
void* threadpool< T >::worker( void* arg )//
{
    threadpool* pool = ( threadpool* )arg;
    pool->run();
    return pool;
}

template< typename T >
void threadpool< T >::run() {
    while (!m_stop) {//工作线程会一直循环，直到m_stop设置为true才退出
        m_queuestat.wait();//信号量减一，初始时信号量为0，线程池中的每个线程将在此阻塞
        m_queuelocker.lock();//每个线程共享同一个对象，操作时要加锁
        if ( m_workqueue.empty() ) {//如果m_queuestat是默认初始化（信号量初始为0），可将这个if注释掉
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop();
        m_queuelocker.unlock();
        request->process();
    }
}

#endif
