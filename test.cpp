#include <cstdio>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
// #include <sys/un.h>
#include <netinet/in.h>
#include <error.h>
#include <cstdlib>
#include <fcntl.h>
#include <cassert>
#include <signal.h>
#include <cstring>
#include <map>
#include <string>
using namespace std;
typedef string (*method)(string);
int main(){
    map<string,method> mp;



    return 0;
}
// const int port = 12306;
// const char * ip = "127.0.0.1";
// static int pipefd[2];
// int setnonblocking(int fd) {
//     int old_opt = fcntl(fd,F_GETFL);
//     int new_opt = old_opt|O_NONBLOCK;
//     fcntl(fd,F_SETFL,new_opt);
//     return old_opt;
// }
// void addfd(int epollfd,int fd) {
//     struct epoll_event event;
//     event.data.fd = fd;
//     event.events = EPOLLIN;
//     epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
//     setnonblocking(fd);
// }

// void alarm_handler(int sig) {
//     printf("alarm_handler已经执行\n");
//     send(pipefd[1],(char *)&sig,1,0);
// }
// void addsig(int sig,void(handler)(int),bool restart = true) {
//     struct sigaction sa;
//     memset(&sa,'\0',sizeof(sa));
//     sa.sa_handler = handler;
//     if(restart) {
//         sa.sa_flags |= SA_RESTART;
//     }
//     sigfillset(&sa.sa_mask);
//     assert(sigaction(sig,&sa,NULL) != -1);
// }
// int main() {

//     struct sockaddr_in addr;
//     int listenfd,epollfd;
//     int ret = 0;
//     listenfd = socket(AF_INET,SOCK_STREAM,0);
//     if(listenfd == -1) {
//         perror("listenfd");
//         exit(-1);
//     }
    
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(port);
//     inet_pton(AF_INET,ip,&addr.sin_addr);
//     ret = bind(listenfd,(struct sockaddr *)&addr,sizeof(addr));
//     if(ret < 0) {
//         perror("bind");
//         exit(-1);
//     }
//     ret = listen(listenfd,5);
//     if(ret < 0) {
//         perror("listen:");
//         exit(-1);
//     }
//     epollfd = epoll_create(5);
//     struct epoll_event events[1024];
//     addfd(epollfd,listenfd);
//     ret = socketpair(AF_UNIX,SOCK_STREAM,0,pipefd);
//     setnonblocking(pipefd[1]);
//     addfd(epollfd,pipefd[0]);
//     addsig(SIGALRM,alarm_handler);
//     char buf[1024];
//     alarm(5);
//     while(1) {
//         int numbers = epoll_wait(epollfd,events,1024,-1);
//         printf("numbers:%d\n",numbers);
//         for(int i = 0; i < numbers;i++) {
//             int sockfd = events[i].data.fd;
//             if(sockfd == listenfd) {
//                 //接受请求
//                 sockaddr_in caddr;
//                 socklen_t len = sizeof(caddr);
//                 int connfd = accept(sockfd,(struct sockaddr *)&caddr,&len);
//                 if(connfd < 0) {
//                     perror("accept");
//                     exit(-1);
//                 }
//                 addfd(epollfd,connfd);
//                 // ret = read(connfd,buf,sizeof(buf));
//                 // printf(buf);
//             }else if((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)){
//                 char sig[1024];
//                 int ret = recv(pipefd[0],sig,sizeof(sig),0);
//                 for(int i = 0;i < ret;i++) {
//                     switch(sig[i]) {
//                         case SIGALRM:
//                         {
//                             //接收到信号
//                             printf("接受到了定时信号\n");
//                             alarm(5);
//                             break;
//                         }
//                     }
//                 }
//                 // ret = read(sockfd,buf,sizeof(buf));
//                 // printf("buf:%s\n",buf);
//             }
//         }
//     }
    
    
// }
