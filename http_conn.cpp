#include "http_conn.h"
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>//close
#include <cstring>

//必须定义，否则提示：无法解析外部符号
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 网站的根目录
const char* doc_root = "/home/dahling/tcpcoding/resources";

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr){
    m_sockfd = sockfd;
    m_address = addr;
    
    // 端口复用
    int reuse = 1;
    //SO_REUSEPOR和SO_REUSEADDR一样的
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

    epoll_event event;
    event.data.fd = m_sockfd;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;//EPOLLONESHOT
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_sockfd, &event);//向epfd中注册serv_sock，监听EPOLLIN(可读)。CTL：control
    int flag = fcntl(m_sockfd, F_GETFL, 0);//设置非阻塞
    fcntl(m_sockfd, F_SETFL, flag | O_NONBLOCK);

    m_user_count++;
    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;   // 初始状态为检查请求行
    m_linger = false; // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;   // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);//读缓冲
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
    bytes_to_send = 0;
    bytes_have_send = 0;
}


// 关闭连接
void http_conn::close_conn() {
    if(m_sockfd != -1) {
        epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_sockfd,0);
        close(m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 关闭一个连接，将客户总数量-1
    }
}

//非阻塞读，每次读要将所有数据一次性读出
bool http_conn::read() {
    int bytes_read = 0;
    while(true) {
        // 从m_read_idx索引处开始保存数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {// 非阻塞读的特殊处理，没有数据时
                break;
            }
            close_conn();//连接异常，关闭连接
            return false;   
        } 
        else if (bytes_read == 0) {   //对方正常关闭，发来FIN
            close_conn();
            return false;
        }
        m_read_idx += bytes_read;
    }
    //printf("读到的数据：\n %s\n", m_read_buf);
    return true;
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

// 写HTTP响应
bool http_conn::write()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();//发送影响之后，读缓冲置为0
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            if( m_file_address ){
                munmap( m_file_address, m_file_stat.st_size );
                m_file_address = 0;
            }
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            if( m_file_address ){
                munmap( m_file_address, m_file_stat.st_size );
                m_file_address = 0;
            }
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }

    }
}


// 遍历一行，将\r\n设置为字符串结束符\0\0
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) {
            if ( ( m_checked_idx + 1 ) == m_read_idx ) {//如果只有'\r'
                return LINE_OPEN;//数据不完整
            } 
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) {
                m_read_buf[ m_checked_idx++ ] = '\0';//将\r\n改为\0\0，字符串结束符
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } 
        // \r\n\n时
        else if( temp == '\n' )  {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret= NO_REQUEST;
    LINE_STATUS line_status = LINE_OK;
    char* text = nullptr;//指向数据
    while (1) {
        //如果是在解析请求主体，就不再调用parse_line(用来解析请求行或头)
        if(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)){

            // 获取一行数据
            text = m_read_buf + m_start_line;//text指向要解析的那一行数据
            m_start_line = m_checked_idx;//指向下一个要解析的行
            printf( "got 1 http line: %s\n", text );//printf遇到\n即停止

            switch ( m_check_state ) {
                case CHECK_STATE_REQUESTLINE: {//处理请求行：GET /index.html HTTP/1.1:方法，请求文件，HTTP版本号
                    //可以使用正则，这里手动方式解析字符串
                    m_url = strpbrk(text, " \t"); // 第一个空格所在位置
                    if (! m_url) { 
                        read_ret  = BAD_REQUEST;
                        break;
                    }   
                    *m_url++ = '\0';    // 置位空字符，字符串结束符:GET\0/index.html HTTP/1.1
                    char* method = text;
                    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
                        m_method = GET;
                    } 
                    else {
                        read_ret  = BAD_REQUEST;
                        break;
                    }
                    m_version = strpbrk( m_url, " \t" );
                    if (!m_version) {
                        read_ret  = BAD_REQUEST;
                        break;
                    }
                    *m_version++ = '\0'; // GET\0/index.html\0HTTP/1.1
                    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
                        read_ret  = BAD_REQUEST;
                        break;
                    }

                    if (strncasecmp(m_url, "http://", 7) == 0 ) {  //如果m_url开头是 "http://"，即请求行中的目标文件为：http://10.147.80.4：9190/index.html
                        m_url += 7;
                        m_url = strchr( m_url, '/' );//找'/'第一次出现的位置
                    }
                    if ( !m_url || m_url[0] != '/' ) {
                        read_ret  = BAD_REQUEST;
                        break;
                    }
                    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查请求头
                    read_ret = NO_REQUEST;
                    break;
                }
                case CHECK_STATE_HEADER: {//处理请求头
                    if( text[0] == '\0' ) {//所在while循环处理请求头，如果遇到空行
                        if ( m_content_length != 0 ) {//如果有消息体
                            m_check_state = CHECK_STATE_CONTENT;// 状态机转移到CHECK_STATE_CONTENT状态，开始处理消息体
                            read_ret = NO_REQUEST;//还没处理完，继续处理
                        }
                        else{//否则没有消息体
                            read_ret = GET_REQUEST;
                        }
                        break;
                    } 
                    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {// 处理Connection 头部字段  Connection: keep-alive
                        text += 11;
                        text += strspn( text, " \t" );
                        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
                            m_linger = true;
                        }
                    } 
                    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
                        text += 15;
                        text += strspn( text, " \t" );
                        m_content_length = atol(text);
                    } 
                    else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
                        text += 5;
                        text += strspn( text, " \t" );
                        m_host = text;
                    } 
                    else {//没有解析的头部就打印出unknown
                        printf( "oop! unknown header %s\n", text );
                    }
                    read_ret = NO_REQUEST;
                    break;
                }
                case CHECK_STATE_CONTENT: {//处理请求体
                    if ( m_read_idx >= ( m_content_length + m_checked_idx ) ){
                        text[ m_content_length ] = '\0';
                        read_ret = GET_REQUEST;;
                        break;
                    }
                    read_ret = NO_REQUEST;//如果没有完全的读入请求体内容
                    line_status = LINE_OPEN;//标记以下，在CHECK_STATE_CONTENT情况下去调用parse_line->去更新m_checked_idx，进而更新m_start_line
                    break;
                }
                default: {
                    read_ret = INTERNAL_ERROR;
                    break;
                }
            }
            if(read_ret == BAD_REQUEST || read_ret == INTERNAL_ERROR || read_ret == GET_REQUEST){//错误：read_ret == BAD_REQUEST || INTERNAL_ERROR || GET_REQUEST
                if(read_ret == GET_REQUEST){//获得了完整的一个请求报文，接下来处理请求实体
                    // 这里是实现get请求：如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
                    strcpy( m_real_file, doc_root );//doc_root: /home/dahling/tcpcoding/resources
                    int len = strlen( doc_root );
                    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
                    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
                    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
                        read_ret = NO_RESOURCE;
                    }
                    else if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {// 判断访问权限
                        read_ret = FORBIDDEN_REQUEST;
                    }
                    else if ( S_ISDIR( m_file_stat.st_mode ) ) {// 判断是否是目录
                        read_ret = BAD_REQUEST;
                    }
                    else{
                        // 以只读方式打开文件
                        int fd = open( m_real_file, O_RDONLY );
                        // 创建内存映射
                        m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
                        close( fd );
                        read_ret = FILE_REQUEST;
                    }
                }
                break;
            }
        }
        else{
            read_ret =  NO_REQUEST;
            break;//退出while循环
        }
    }

    //如果请求不完整，则注册读事件，继续去读数据
    if ( read_ret == NO_REQUEST ) {
        epoll_event event;
        event.data.fd = m_sockfd;
        event.events = EPOLLIN| EPOLLONESHOT | EPOLLRDHUP;//注意：需要重新注册epollooneshot事件！
        epoll_ctl( m_epollfd, EPOLL_CTL_MOD, m_sockfd, &event );
        return;
    }
    
    // 生成响应
    bool write_ret,cntn = true;
    switch (read_ret)
    {
        case FILE_REQUEST:
            add_response( "%s %d %s\r\n", "HTTP/1.1", 200, "OK" );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            write_ret = true;
            cntn = false;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            break;
        case BAD_REQUEST:
            add_response( "%s %d %s\r\n", "HTTP/1.1", 400, "Bad Request" );
            add_headers( strlen( "Your request has bad syntax or is inherently impossible to satisfy.\n" ) );
            if ( !add_response( "%s", "Your request has bad syntax or is inherently impossible to satisfy.\n" ) ) {
                write_ret = false;
                cntn = false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_response( "%s %d %s\r\n", "HTTP/1.1", 403, "Forbidden" );
            add_headers(strlen( "You do not have permission to get file from this server.\n"));
            if ( !add_response( "%s", "You do not have permission to get file from this server.\n" )) {
                write_ret = false;
                cntn = false;
            }
            break;
        case NO_RESOURCE:
            add_response( "%s %d %s\r\n", "HTTP/1.1", 404, "Not Found" );
            add_headers( strlen( "The requested file was not found on this server.\n" ) );
            if ( !add_response( "%s", "The requested file was not found on this server.\n" ) ) {
                write_ret = false;
                cntn = false;
            }
            break;
        case INTERNAL_ERROR://500
            add_response( "%s %d %s\r\n", "HTTP/1.1", 500, "Internal Error" );
            add_headers( strlen( "There was an unusual problem serving the requested file.\n" ) );
            if ( !add_response( "%s", "There was an unusual problem serving the requested file.\n" ) ) {
                write_ret = false;
                cntn = false;
            }
            break;
        default:
            write_ret = false;
            cntn = false;
            break;
    }
    if(cntn){
        m_iv[ 0 ].iov_base = m_write_buf;
        m_iv[ 0 ].iov_len = m_write_idx;
        m_iv_count = 1;
        write_ret = true;
    }


    if ( !write_ret ) {
        close_conn();
    }
    //注册写事件
    epoll_event event;
    event.data.fd = m_sockfd;
    event.events = EPOLLOUT | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( m_epollfd, EPOLL_CTL_MOD, m_sockfd, &event );
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {//可变参数...
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {//如果写缓冲区写满
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

void http_conn::add_headers(int content_len) {
    add_response( "Content-Length: %d\r\n", content_len );
    add_response("Content-Type:%s\r\n", "text/html");
    add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
    add_response( "%s", "\r\n" );
}


