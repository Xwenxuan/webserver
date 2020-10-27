#include "processpool.h"

//使用进程池做一个cgi服务器

class cgi_conn{
public:
    cgi_conn();
    ~cgi_conn(){}

    void init(int epollfd,int sockfd,const sockaddr_int & client_addr) {
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        memset(m_buf,'\0',BUFFER_SIZE);
        m_read_idx = 0;
    }
    void process() {
        int idx = 0;
        int ret = -1;
        /*循环读取和分析客户数据*/
        while(true) {
            idx = m_read_idx;
            ret = recv(m_sockfd,m_buf+idx,BUFFER_SIZE-1-idx,0);
            /*如果发生读错误，则关闭客户连接。但如果是暂时无数据刻度，则退出循环*/
            if(ret < 0) {
                if(errno != EAGIN) {
                    removefd(m_epollfd,m_sockfd);
                }
                break;
            }else if(ret == 0) {
                removefd(m_epollfd,m_sockfd);    
            }
            else {
                m_read_idx += ret;
                printf("user content is : %s\n",m_buf);
                /*如果遇到\r\n 则开始处理客户请求*/
                for(;idx < m_read_idx;idx++) {
                    if(idx >= 1 &&(m_buf[idx-1] == '\r') && (m_buf[idx] == '\n')) {
                        break;
                    }
                }
                /*如果没有遇到"\r\n" ,则需要读取更多的客户数据*/
                if(idx == m_read_idx) {
                    continue;
                }
                m_buf[idx-1] = '\0';
                char * file_name = m_buf;
                /*判断客户要运行的CGI程序是否存在*/
                if(access(file_name,F_OK) == -1) {
                    removefd(m_epollfd,sockfd);
                    break;
                }
                /*创建子进程来执行CGI程序*/
                ret = fork();
                if(ret == -1) {
                    removefd(m_epollfd,sockfd);
                    break;
                }
                else if(ret > 0) {
                    //父进程只需要关闭连接
                    removefd(m_epollfd,m_sockfd);
                    break;
                }else {
                    /*子进程将标准输出定向到m_sockfd,并执行CGI程序*/
                    close(STDOUT_FILENO);
                    dup(m_sockfd);
                    execl(m_buf,m_buf,0);
                    exit(0);
                }

            }
        }
    }
private:
    static const int BUFFER_SIZE = 1024;
    staic int m_epollfd;
    int m_sockfd;
    sockaddr_in m_adress;
    char m_buf[BUFFER_SIZE];
    int m_read_idx;
};
int cgi_conn::m_epollfd = -1;