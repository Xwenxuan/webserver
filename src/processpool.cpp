#include "processpool.h"

//线程池构造函数
template <typename T>
processpool<T> :: processpool(int listenfd,int process_num):m_listenfd(listenfd),m_process_num(process_num),m_idx(-1),m_stop(false) {
    assert((process_num > 0) && (process_num <= MAX_PROCESS_NUMBER));
    m_sub_process = new process[process_num];
    assert(m_sub_process);

    //创建process_num个子进程
    for(int i = 0;i < process_num;i++) {
        //利用socketpair 创建出全双工的管道
        int ret = socketpair(PF_UNIX,SOCK_STREAM,0,m_sub_process[i].m_pipefd);
        assert(ret == 0);
        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid > 0);
        if(m_sub_process[i].m_pid > 0) {
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        }else {
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break;
        }
    }
}

//统一事件源
template <typename T>
void processpool<T>::setup_sig_pipe() {
    //创建epoll 事件监听表和信号管道
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
    int ret = socketpair(PF_INET,SOCK_STREAM,0,sig_pipefd);
    assert(ret != -1);
    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd,sig_pipefd[0]);

    /*设置信号处理函数*/
    addsig(SIGCHLD,sig_handler);
    addsig(SIGTERM,sig_handler);
    addsig(SIGINT,sig_handler);
    addsig(SIGPIPE,SIG_IGN);

}

template <typename T>
void processpool<T> :: run() {
    if(m_idx != -1) {
        run_child();
        return;
    }
    run_parent();
}

template <typename T>
void processpool<T> :: run_child() {
    setup_sig_pipe();

    //每个子进程都通过其在进程池中的序号m_idx找到与父进程通信的管道
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    addfd(m_epollfd,pipefd);
    epoll_event events[MAX_EVENT_NUMBER];
    T * users = new T[USER_PER_PROCESS];
    assert(users);

    int number = 0;
    int ret = -1;

    while(! m_stop ) {
        number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number < 0) && (errno != EINTR)) {
            printf("epoll error\n");
            break;
        }
        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if((scokfd == pipefd) && (events[i].events & EPOLLIN)) {
                //表示是父子进程的管道数据
                int client = 0;
                /*从父子进程管道中拿数据，如果读取成功，则表示有新的客户连接到来*/
                ret = recv(sockfd,(char *)&client,sizeof(client),0);
                if(((ret < 0 ) && (errno != EAGAIN)) || ret == 0) {
                    continue;
                }else {
                    struct sockaddr_in addr;
                    socklen_t client_addr_len = sizeof(addr);
                    int connfd = accept(m_listenfd,(struct sockaddr *)&addr,&client_addr_len);
                    if(connfd < 0) {
                        printf("error is: %d\n",errno);
                        continue;
                    }
                    addfd(m_epollfd,connfd);

                    /*模板类T必须实现init方法，初始化一个客户连接*/
                    users[connfd].init(m_epollfd,connfd,addr);
                }else if((scokfd == sig_pipefd[0]) && (events[i].events && EPOLLIN)) {

                    //处理信号
                    int sig;
                    char signals[1024];
                    ret = recv(sockfd,signals,sizof(signals),0);
                    if(ret <= 0) continue;
                    else {
                        for(int i = 0;i < ret;i++) {
                            switch (signals[i])
                            {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while((pid = waitpid(-1,&stat,WNOHANG)) > 0) {
                                    continue;
                                }
                                break;
                            }
                                
                            case SIGTERM:
                            case SIGINT:
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                                break;
                            }
                        }
                    }
                }else if(events[i].events & EPOLLIN) {
                    //任务处理
                    users[sockfd].process();
                }else {
                    continue;
                }
            }
        }
    }
    delete [] users;
    users = NULL;
    close(pipefd);
    close(m_epollfd);
}

template <typename T>
void processpool<T> :: run_parent() {
    setup_sig_pipe();
    /*父进程监听m_listenfd*/
    addfd(m_epollfd,m_listenfd);
    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 0;
    int number = 0;
    int ret = -1;
    while(!m_stop) {
        number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number < 0) && (errno != EINTR)) {
            printf("epoll fail\n");
            break;
        }
        for(int i =0;i < number;i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == m_listenfd) {
                /*如果有新的连接到来，就采用Round Robin方式将其分批给一个子进程处理  其实就是循环队列*/
                int i = sub_process_counter;
                do{
                    if(m_sub_process[i].m_pid != -1) {
                        break;
                    }
                    i = (i+1)%m_process_number;
                }
                while(i != sub_process_number);
                if(m_sub_process[i].m_pid == -1) {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1) % m_process_number;
                send(m_sub_process[i].m_pipefd[0],(char *)&net_conn,sizeof(new_conn),0);
                printf("send request to child %d\n",i);
            }
            else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {
                //处理父进程接受到的信号
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0],signals,sizeof(signals),0);
                if(ret <= 0) {
                    continue;
                }
                else {
                    for(int i = 0;i < ret;i++) {
                        switch (signals[i])
                        {
                        case SIGCHLD:
                        {
                            pid_t pid;
                            int stat;
                            while((pid = waitpid(-1,&stat,WNOHANG)) > 0) {
                                for(int i = 0;i< m_process_number;i++) {
                                    if(m_sub_process[i].m_pid == pid) {
                                        printf("child %d join\n",i);
                                        close(m_sub_process[i].m_pipefd[0]);
                                        m_sub_process[i].m_pid = -1;
                                    }
                                }
                            }
                            /*如果所有进程都已经退出了，父进程也退出*/
                            m_stop = true;
                            for(int i =0;i < m_process_number;i++) {
                                if(m_sub_process[i].m_pid != -1) {
                                    m_stop = false;
                                }
                            }
                            break;
                        }
                            
                        case SIGTERM:
                        case SIGINT:{
                            /*父进程接到了终止信号，就杀死所有的子进程*/
                            printf("kill all the child now\n");
                            for(int i = 0;i < m_process_num;i++) {
                                int pid = m_sub_process[i].m_pid;
                                if(pid != -1) {
                                    kill(pid,SIGTERM);
                                }
                            }
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }else continue;
        }
    }
    close(m_epollfd);
}