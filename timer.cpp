#include "timer.h"

sort_timer_lst::~sort_timer_lst(){
    util_timer* tmp = head;
    while( tmp ) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::tick(){
    if( !head ) {
        return;
    }
    printf( "timer tick\n" );
    time_t cur = time( NULL );  // 获取当前系统时间
    util_timer* tmp = head;
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while( tmp ) {
        /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
        比较以判断定时器是否到期*/
        if( cur < tmp->expire ) {
            break;
        }

        // 调用定时器的回调函数，以执行定时任务
        tmp->user_data->close_conn();
        // 执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
        head = tmp->next;
        if( head ) {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}