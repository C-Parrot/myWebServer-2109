#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <sys/socket.h>
#include <string>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>


class http_conn{
    
public:

    http_conn(){};
    ~http_conn(){};

    void init(int sockfd, const sockaddr_in &addr);
    void close_conn();  // 关闭连接
    bool read();// 读数据，非阻塞
    void process();//处理请求，业务逻辑
    bool write();// 响应，非阻塞



    static int m_epollfd;
    static int m_user_count; // 记录有多少个用户
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的大小

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
    解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUESTLINE:当前正在分析请求行
    CHECK_STATE_HEADER:当前正在分析头部字段
    CHECK_STATE_CONTENT:当前正在解析请求体*/
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // LINE_OK:读取到一个完整的行 
    //LINE_BAD:行出错 
    //LINE_OPEN行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
    服务器处理HTTP请求的可能结果，报文解析的结果
    NO_REQUEST          :   1XX，请求数据还没读完，继续处理读取
    GET_REQUEST         :   xXX，所有的请求数据已全部读取
    BAD_REQUEST         :   400，表示客户请求语法错误
    NO_RESOURCE         :   404，服务器没有资源
    FORBIDDEN_REQUEST   :   401，表示客户对资源没有足够的访问权限
    FILE_REQUEST        :   文件请求,获取文件成功
    INTERNAL_ERROR      :   500，服务器执行时发生错误
    CLOSED_CONNECTION   :   表示客户端已经关闭连接了*/
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    


private:

    void init();
    LINE_STATUS parse_line(); //解析一行
    bool add_response( const char* format, ... );//返回响应
    void add_headers( int content_length );


    int m_sockfd; // 该HTTP连接的socket和对方的socket地址
    sockaddr_in m_address;

    char m_read_buf[ READ_BUFFER_SIZE ];    // 读缓冲区
    int m_read_idx; // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    //解析
    int m_checked_idx;  // 已验证的数据的下一个位置
    int m_start_line;  // 指向当前正在读取的行的下一行的位置

    CHECK_STATE m_check_state; // 主状态机当前所处的状态
    char m_real_file[ FILENAME_LEN ];  // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    METHOD m_method; // 请求方法
    char* m_url; // 请求的目标文件的文件名
    char* m_version; // HTTP协议版本号，我们仅支持HTTP1.1
    
    char* m_host; // 主机名
    int m_content_length; // HTTP请求的消息总长度
    bool m_linger; // HTTP请求是否要求保持连接

    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区,放响应行，头
    int m_write_idx;   // 写缓冲区中待发送的字节数
    char* m_file_address;  // 客户请求的目标文件被mmap到内存中的起始位置，响应体
    struct stat m_file_stat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2]; // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。（响应头，响应体）
    int m_iv_count;
    int bytes_to_send;  // 将要发送的数据的字节数
    int bytes_have_send;  // 已经发送的字节数
};


#endif
