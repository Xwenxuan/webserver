#ifndef LST_TIMER
#define LST_TIMER
#include <time.h>
#include <netinet/in.h>
#include <stdio.h>
#include "http_conn.h"
#define BUFFER_SIZE 64
/**
 * 升序定时器链表
 * 用来处理非活跃的连接
 * 
 * */
class util_timer; /*前向声明*/
class http_conn;
// /*用户数据结构*/
// struct client_data
// {
//     sockaddr_in address;
//     int sockfd;
//     char buf[BUFFER_SIZE];
//     util_timer * timer;
// };


/*定时器类*/
class util_timer{
public:
    util_timer():prev(NULL),next(NULL){}

public:
    time_t expire; //任务超时时间
    void (*cb_func)(http_conn * user_data);//任务回调函数
    http_conn * user_data;
    util_timer * prev;
    util_timer * next;
};

/*定时器双向链表*/
class sort_timer_lst{
public:
    sort_timer_lst():head(NULL),tail(NULL){}
    ~sort_timer_lst(){
        util_timer * tmp = head;
        while(tmp) {
            head = tmp -> next;
            delete tmp;
            tmp = head;
        }
    }

    /*将定时器加到链表当中*/
    void add_timer(util_timer * timer) {
        if(!timer) return;
        if(!head) {
            head = tail = timer;
            return;
        }
        /**
         * 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间
         * 则把该定时器插入到链表头部
         * 否则就把它插入到合适的位置
         * 
         * */
        if(timer -> expire < head -> expire) {
            timer -> next  = head;
            head -> prev = timer;
            head = timer;
            return;
        }
        add_timer(timer,head);
    }
    /*当某个定时器任务发生变化时，调整对应定时器在链表中的位置*/
    void adjust_timer(util_timer * timer) {
        if(!timer) return;
        util_timer * tmp = timer -> next;
        //如果该定时器就在尾部 或新的超时值任小于下一个定时器的超时值 就不用变
        if(!tmp || (timer -> expire < tmp -> expire)) {
            return ;
        }

        //如果目标定时器时链表的头节点，则将该定时器从链表取出并重新插入链表
        if(timer == head) {
            head = head -> next;
            head -> prev = NULL;
            timer -> next = NULL;
            add_timer(timer,head);
        }else {
            //目标定时器不是头节点 插入到所在位置之后的表中
            timer -> prev -> next = timer -> next;
            timer -> next -> prev = timer -> prev;
            add_timer(timer,head);
        }
    }
    //将目标定时器删除
    void del_timer(util_timer * timer) {
        if(!timer) return;
        //只有一个定时器的情况
        if((head == timer) && (tail == timer)) {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        //是头节点的情况下
        if(head == timer) {
            head = head -> next;
            head -> prev = NULL;
            delete timer;
            return;
        }
        if(tail == timer) {
            tail = tail -> prev;
            tail -> next = NULL;
            delete timer;
            return;
        }
        //位于链表中间
        timer -> prev -> next = timer -> next;
        timer -> next -> prev = timer -> prev;
        delete timer;
    }
    /*SIGALRM信号每次出发就执行一次tick函数*/
    void tick() {
        if(!head) {
            return;
        }
        printf("timer tick\n");
        time_t cur = time(NULL);
        util_timer * tmp = head;
        /*从头节点开始处理每个处理器*/
        while(tmp) {
            //判断是否到期
            if(cur < tmp->expire) {
                break;
            }
            /*调用定时器回调函数 执行定时任务*/
            tmp -> cb_func(tmp -> user_data);
            head = tmp -> next;
            if(head) {
                head -> prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }
private:
    //把定时器插到中间节点中
    void add_timer(util_timer * timer,util_timer * head) {
        util_timer * prev = head;
        util_timer * tmp = prev -> next;
        while(tmp) {
            if(timer -> expire < tmp -> expire) {
                prev -> next = timer;
                timer -> next = tmp;
                tmp -> prev = timer;
                timer -> prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp -> next;
        }
        if(!tmp) {
            prev -> next = timer;
            timer -> prev = prev;
            timer -> next = NULL;
            tail = timer;
        }
    }
private:
    util_timer * head;
    util_timer * tail;
};
#endif