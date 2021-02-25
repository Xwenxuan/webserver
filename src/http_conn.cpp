#include "http_conn.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include<string>
using namespace std;
/*定义http响应的一些状态信息*/
const char * ok_200_title = "OK";
const char * error_400_title = "Bad Request";
const char * error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char * error_403_title = "Forbidden";
const char * error_403_form = "You do not have permission to get file from this server.";
const char * error_404_form = "The requested file was not found on this server.\n";
const char * error_404_title = "Not Found";
const char * error_500_title = "Internal Error";
const char * error_500_form = "There was an unusual problem serving the requested file.\n";
/*网站根目录*/
const char * doc_root = "/var/www/html";
int setnonblocking(int fd) {
    int old_opt = fcntl(fd,F_GETFL);
    int new_opt = old_opt|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_opt);
    return old_opt;
}
void addfd(int epollfd,int fd,bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN|EPOLLET|EPOLLRDHUP;
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}
void removefd(int epollfd,int fd) {
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd ,0);
    shutdown(fd,SHUT_RDWR);
}

void modfd(int epollfd,int fd,int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev|EPOLLIN|EPOLLET|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close) {
    if(real_close && (m_sockfd != -1)) {
        removefd(m_epollfd,m_sockfd);
        m_sockfd = -1;
        m_user_count--;//关闭一个连接时，将客户总量--
    }
}
void http_conn::init(int sockfd,const sockaddr_in &addr,util_timer * timer) {
    m_sockfd = sockfd;
    m_address = addr;
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));//这一行设置不管TIME_WAIT状态
    addfd(m_epollfd,sockfd,true);
    m_user_count++;
    //定时器
    this -> timer = timer;
    init();
}

void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_read_file,'\0',FILENAME_LEN);

}

/*从状态机   解析行*/
http_conn::LINE_STATUS http_conn::parse_line() {
    char tmp;
    for(;m_checked_idx < m_read_idx;m_checked_idx++) {
        tmp = m_read_buf[m_checked_idx];
        if(tmp == '\r') {
            if((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_idx+1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(tmp == '\n') {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r') {
                m_read_buf[m_checked_idx -1] = '\0';
                m_read_buf[m_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/*循环读取客户数据，直到无数据可读或者对方关闭连接*/
bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read == -1) {
            //发生在非阻塞
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        }
        else if(bytes_read == 0) return false;
        m_read_idx += bytes_read;
    }
    printf("read buf:%s\n",m_read_buf);
    return true;
}
/*
http由请求行 请求头 空行和 请求体组成
请求行：请求方法+空格+URL+空格+协议版本+换行回车
请求头： 键值对
空行
请求体 
*/

/*解析http请求行，获得请求方法，目标URL，以及HTTP版本号*/
http_conn::HTTP_CODE http_conn::parse_request_line(char * text) {
    m_url = strpbrk(text," "); //找目标字符串在源字符串中的位置 并返回那个字符串   好像不是用制表符分隔的 而是用空格分隔
    if(!m_url) return BAD_REQUEST;
    //已经解析出url之后的东西了
    *m_url++ ='\0';
    char * method = text;
    if(strcasecmp(method,"GET") == 0) {
        m_method = GET;
    }else if(strcasecmp(method,"POST") == 0){//新增post请求解析
        m_method = POST;
    }else {
        return BAD_REQUEST;
    }
    // printf("url:%s\n",m_url);
    //这样的到的是版本
    m_version = strpbrk(m_url," ");
    *m_version++ = '\0';
    
    if(!m_version) {
        return BAD_REQUEST;
    }
    
    printf("version:%s\n",m_version);
    if(strcasecmp(m_version,"HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url,"/",1) == 0) {
        printf("m_url:%s\n",m_url);
    }

    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/*解析http请求的一个头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers(char * text) {
    if(text[0] == '\0') {
        //遇到空行 表示头部字段解析完毕
        /*如果http请求有消息体，则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态*/
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        /*否则说明我们已经得到了一个完整的http请求*/
        return GET_REQUEST;
    }
    //处理connection 头部字段
    else if (strncasecmp(text,"Connection:",11) == 0) {
        text += 11;
        text += strspn(text," ");
        if(strcasecmp(text,"keep-alive") == 0) {
            m_linger = true;
        }
    }
    //处理content-length头部字段
    else if(strncasecmp(text,"Content-Length:",15) == 0) {
        text += 15;
        text += strspn(text," ");
        m_content_length = atol(text);
    }
    //处理host头部字段
    else if(strncasecmp(text,"Host:",5) == 0) {
        text += 5;
        text += strspn(text," ");
        m_host = text;
        printf("host:%s\n",m_host);
    }else {
        printf("oop! unknow header %s\n",text);
    }
    return NO_REQUEST;


}

//不解析真正的消息体，只是判断它是否完整的被读入了
http_conn ::HTTP_CODE http_conn::parse_content(char * text) {
    if(m_read_idx >= (m_content_length+m_checked_idx)) {
        //完整读入了 做请求
        text[m_content_length] = '\0';
        if(m_method == GET)
        return GET_REQUEST;
        else if(m_method == POST ) return POST_REQUEST;//新增POST请求处理
    }
    return NO_REQUEST;
}
/*主状态机*/
http_conn :: HTTP_CODE http_conn::process_read() {
    // printf("buf:%s\n",m_read_buf);
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || (line_status = parse_line()) == LINE_OK){
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line:%s\n",text);
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                // printf("准备解析请求行\n");
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

//处理post请求
http_conn::HTTP_CODE http_conn::do_post() {

}

/*当得到一个完整、正确的http请求时，我们就分析目标文件的属性。如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功*/

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_read_file,doc_root);
    int len = strlen(doc_root);
    strncpy(m_read_file+len,m_url,FILENAME_LEN-len-1);
    printf("file: %s\n",m_read_file);
    if(stat(m_read_file,&m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if(!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    printf("正常读取到了文件\n");
    int fd = open(m_read_file,O_RDONLY);
    m_file_address = (char *)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}

/*对内存映射区执行munmap操作*/
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*写http响应*/
bool http_conn::write()
{
    printf("已经开始发送response\n");
    int tmp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if(bytes_to_send == 0) {
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }
    while(1) {
        tmp = writev(m_sockfd,m_iv,m_iv_count);
        if(tmp <= -1) {
            /*如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件。虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性*/
            if(errno == EAGAIN) {
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= tmp;
        bytes_have_send += tmp;
        if(bytes_to_send <= bytes_have_send) {
            printf("发送成功了\n");
            /*发送http响应成功，根据http请求中的Connection字段决定是否立即关闭连接*/
            unmap();
            if(m_linger) {
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
        }else {
            modfd(m_epollfd,m_sockfd,EPOLLIN);
            return false;
        }
    }
}
/*向缓冲区中写入待发送的数据*/
bool http_conn::add_response(const char * format,...) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    //这一块就组织 格式的
    va_list arg_list;
    va_start(arg_list,format);
    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    if(len >= (WRITE_BUFFER_SIZE-1-m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status,const char * title) {
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}
bool http_conn::add_content_type(const char * type) {
    add_response("Content-Type:%s\r\n",type);
}
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    // add_linger();
    
    string type(m_read_file);
    if(type.find(".jpg") != type.npos) {
        add_content_type("text/jpeg");
    }else if(type.find(".mp4") != type.npos) {
        add_content_type("audio/mp4");
    }else {
        add_content_type("text/html");
    }
    
    add_blank_line();
}

bool http_conn:: add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n",content_len);
}
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n",(m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char * content) {
    return add_response("%s",content);
}

/*根据服务器处理http请求的结果，决定返回给客户端的内容*/
bool http_conn::process_write(HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,ok_200_title);
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else {
                const char * ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 线程池调用的函数
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return ;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}