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
const int port = 12306;
const char * ip = "192.168.127.135";
void addfd(int epollfd,int fd) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
}
int main() {

    struct sockaddr_in addr;
    int listenfd,epollfd;
    int ret = 0;
    listenfd = socket(AF_INET,SOCK_STREAM,0);
    if(listenfd == -1) {
        perror("listenfd");
        exit(-1);
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET,ip,&addr.sin_addr);
    ret = bind(listenfd,(struct sockaddr *)&addr,sizeof(addr));
    if(ret < 0) {
        perror("bind");
        exit(-1);
    }
    ret = listen(listenfd,5);
    if(ret < 0) {
        perror("listen:");
        exit(-1);
    }
    epollfd = epoll_create(5);
    struct epoll_event events[1024];
    struct epoll_event event;
    event.data.fd = listenfd;
    event.events = EPOLLIN;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,listenfd,&event);
    char buf[1024];
    while(1) {
        int numbers = epoll_wait(epollfd,events,1024,0);
        // printf("numbers:%d\n",numbers);
        for(int i = 0; i < numbers;i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd) {
                //接受请求
                sockaddr_in caddr;
                socklen_t len = sizeof(caddr);
                int connfd = accept(sockfd,(struct sockaddr *)&caddr,&len);
                if(connfd < 0) {
                    perror("accept");
                    exit(-1);
                }
                addfd(epollfd,connfd);
                // ret = read(connfd,buf,sizeof(buf));
                // printf(buf);
            }else {
                ret = read(sockfd,buf,sizeof(buf));
                printf("buf:%s\n",buf);
            }
        }
    }
    
    
}
