#ifndef __PROCESSPOOL_H
#define __PROCESSPOOL_H
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include<arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

//一个进程类 m_pid指的是对应的进程id pipefd 用来父子进程通信
class process {
public:
    process() : m_pid(-1);
    pid_t m_pid;
    int m_pipefd[2];
};
//进程池类
template <typename T>
class processpool {
private:
    //构造函数设为私有 通过create静态函数 来创建实例
    processpool(int listenfd,int process_num = 8);
public:
    /*单例模式，最多一个进程池*/
    static processpool<T> * create(int listenfd,int process_num = 8) {
        if(!m_instance) {
            m_instance = new processpool<T>(listenfd,process_num);
        }else {
            return m_instance;
        }
    }
    ~processpool() {
        delete []m_sub_process;
    }
    //启动进程池
    void run();
private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    /*最大子进程数量*/
    static const int MAX_PROCESS_NUMBER = 16;
    /*每个子进程最多能处理的客户数量*/
    static const int USER_PER_PROCESS = 65536;
    /*epoll最多能处理的事件数*/
    static const int MAX_EVENT_NUMBER = 1000;

    /*进程池中的进程总数*/
    int m_process_num;
    /*子进程在池中的序号，从0开始*/
    int m_idx;
    /*每个进程都有一个epoll内核事件表，用m_epollfd标识*/
    int m_epollfd;
    /*监听socket*/
    int m_listenfd;
    /*子进程通过m_stop来决定是否停止*/
    int m_stop;
    /*保存子进程的描述信息*/
    process * m_sub_process;
    /*静态进程池实例*/
    static processpool<T> * m_instance;
};


//用于处理信号的管道
static int sig_pipefd[2];

//描述符设置非阻塞
static int setnonblocking(int fd)  {
    int old_opt = fcntl(fd,F_GETFL);
    int new_opt = old_opt|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_opt);
    return old_opt;
}

//把fd加入epoll监听事件中
static void addfd(int epollfd,int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.evnets = EPOLLIN|EPOLLET;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&evnet);
    setnonblocking(fd);
}

//删除事件
static void removefd(int epollfd,fd) {
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//信号处理
static void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1],(char *)&msg,0);
    errno = save_errno;
}

static void addsig(int sig,void(handler)(int),bool restart = true) {
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL) != -1);
}
#endif;