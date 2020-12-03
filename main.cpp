#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "timer.h"
#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000
#define PORT 12306
#define IP "192.168.127.158"
#define TIMESLOT 30

static sort_timer_lst timer_lst;//定时器
static int pipefd[2];//用来传递信号的管道
extern void removefd(int epollfd,int fd);
// void removefd(int epollfd,int fd) {
//     epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    
// } 

/*定时器回调函数*/
void cb_func(http_conn * data) {
    if(data -> get_sockfd() != -1) {
        printf("close fd:&d\n",data -> get_sockfd());
        removefd(data->m_epollfd,data -> get_sockfd());
        assert(data);
        data -> set_sockfd();
        data -> m_user_count --;
    }
    

}

void timer_handler() {
    //定时处理任务，实际上就是调用tick函数
    timer_lst.tick();
    //重新定时
    alarm(TIMESLOT);
}

void addsig(int sig,void(handler)(int),bool restart = true) {
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL) != -1);
}

void show_error(int connfd,const char * info) {
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}
extern void addfd(int epollfd,int fd,bool);
int main() {
    //忽略SIGPIPE信号
    addsig(SIGPIPE,SIG_IGN);
    threadpool<http_conn> *pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){
        return 1;
    }

    //预先为每个可能的客户连接分配一个http_conn对象
    http_conn * users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;
    int listenfd = socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd >= 0);
    struct linger tmp = {1,0};
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    int ret = 0;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET,IP,&addr.sin_addr);

    ret = bind(listenfd,(struct sockaddr *)&addr,sizeof(addr));
    assert(ret >= 0);

    ret = listen(listenfd,5);
    assert(ret >= 0);
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(100);
    assert(epollfd != -1);
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd = epollfd;

    alarm(TIMESLOT);
    //是否时间到
    bool timeout = false;
    while(true) {
        int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        printf("有%d个sockfd的请求\n",number);
        if(number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }
        for(int i = 0;i < number;i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd) {
                //接受请求的fd
                struct sockaddr_in caddr;
                socklen_t len = sizeof(caddr);
                int connfd = accept(listenfd,(struct sockaddr*)&caddr,&len);
                if(connfd < 0) {
                    printf("error is:%d\n",errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD) {
                    show_error(connfd,"Internal server busy");
                    continue;
                }

                /*创建定时器*/
                util_timer * timer = new util_timer;
                timer ->user_data = &users[connfd];
                time_t cur = time(NULL);
                timer -> expire = cur + 3*TIMESLOT; //设置过期时间
                timer -> cb_func = cb_func; //设置回调函数
                /*初始化客户端连接*/
                users[connfd].init(connfd,caddr,timer);
                //加到定时器链表中
                timer_lst.add_timer(timer);
            }else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR)) {
                //对端关闭了
                printf("对端关闭\n");
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN) {
                //读事件
                printf("-------------接到了请求-------------------\n");
                if(users[sockfd].read()) {
                    //读到了数据 调整定时器 其实就是重新定时
                    time_t cur = time(NULL);
                    util_timer *timer = users[sockfd].timer;
                    timer -> expire += cur + 3*TIMESLOT;
                    printf("adjust timer once\n");
                    timer_lst.adjust_timer(timer);

                    pool -> append(users+sockfd);
                }else {
                    //读数据有错 就移除定时器
                    timer_lst.del_timer(users[sockfd].timer);
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT) {
                printf("-------------要写入访问信息了-------------------\n");
                if(!users[sockfd].write()) {
                    //读数据有错 就移除定时器
                    timer_lst.del_timer(users[sockfd].timer);
                    printf("-----------------------准备关闭连接---------------");
                    users[sockfd].close_conn();
                }
                //还活着 调整定时器 其实就是重新定时
                time_t cur = time(NULL);
                util_timer *timer = users[sockfd].timer;
                timer -> expire = cur + 3*TIMESLOT;
                printf("adjust timer once\n");
                timer_lst.adjust_timer(timer);
            }else if((sockfd == pipefd[1]) && (events[i].events & EPOLLOUT)) {
                //接收到信号  这叫做统一事件源
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0],signals,sizeof(signals),0);
                if(ret == -1) {
                    continue;
                }else if(ret == 0) {
                    continue;
                }else {
                    for(int i = 0;i < ret; i++) {
                        switch(signals[i]) {
                            case SIGALRM:{
                                printf("接收到了定时器信号\n");
                                timeout = true;
                                break;
                            }

                        }
                    }
                }
                

            }
            if(timeout) {
                timer_handler();
                timeout = false;
            }
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete []users;
    delete pool;
    return 0;
}