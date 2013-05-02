
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

struct ngx_listening_s {
    ngx_socket_t        fd;

    struct sockaddr    *sockaddr;
    socklen_t           socklen;    /* size of sockaddr */
    size_t              addr_text_max_len;
    ngx_str_t           addr_text;//���SOCK���ַ�������127.0.0.1:8008

    int                 type;

    int                 backlog;
    int                 rcvbuf;
    int                 sndbuf;

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler;//�Զ���Ļص�������ս��ܵ������ӣ�һ��Ϊngx_http_init_connection

    void               *servers;  /* array of ngx_http_in_addr_t, for example *///ָ��ngx_http_port_t����������������˿ڴ����server

    ngx_log_t           log;
    ngx_log_t          *logp;

    size_t              pool_size;
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;
    /* should be here because of the deferred accept */
    ngx_msec_t          post_accept_timeout;

    ngx_listening_t    *previous;
    ngx_connection_t   *connection;

    unsigned            open:1;
    unsigned            remain:1;
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    unsigned            listen:1;
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    unsigned            addr_ntop:1;

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned            ipv6only:2;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT)
    unsigned            deferred_accept:1;//�Ƿ񲻵ȴ��ͻ��˵�ACK������ֱ�ӵȴ���һ�����ݰ�
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#ifdef SO_ACCEPTFILTER
    char               *accept_filter;
#endif
#endif
#if (NGX_HAVE_SETFIB)
    int                 setfib;
#endif

};


typedef enum {
     NGX_ERROR_ALERT = 0,
     NGX_ERROR_ERR,
     NGX_ERROR_INFO,
     NGX_ERROR_IGNORE_ECONNRESET,
     NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
     NGX_TCP_NODELAY_UNSET = 0,
     NGX_TCP_NODELAY_SET,
     NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
     NGX_TCP_NOPUSH_UNSET = 0,
     NGX_TCP_NOPUSH_SET,
     NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01


struct ngx_connection_s {
    void               *data;//Ϊ��ʱ����������free_connections�б��dataָ����һ�����ӡ���ʹ��ʱ��ָ��ngx_http_connection_t�ṹ
    			//����dataָ������Ӧ������ngx_http_request_t//��ס��������������ĸ�����
    ngx_event_t        *read;//������ӵĶ�д�¼��ṹ
    ngx_event_t        *write;//������ӵĶ�д�¼��ṹ

    ngx_socket_t        fd;//������ӵľ����������Ӱһ�£�

    ngx_recv_pt         recv;//��������ϵĶ�д�ص���ngx_event_accept����������ӵ�ʱ��ֵngx_unix_recv
    ngx_send_pt         send;//ngx_unix_send
    ngx_recv_chain_pt   recv_chain; //ngx_readv_chain .
    ngx_send_chain_pt   send_chain;//ngx_writev_chain
    ngx_listening_t    *listening;//�ҵ��ϼ�����SOCK���ܶ����ӻ�ָ��һ������LISTENING�ṹ

    off_t               sent;//��������Ϸ��ͳ�ȥ�����ݴ�С

    ngx_log_t          *log;

    ngx_pool_t         *pool;

    struct sockaddr    *sockaddr;//�ͻ��˵�ַ
    socklen_t           socklen;
    ngx_str_t           addr_text;//��ַ�Ŀɶ��ַ���

#if (NGX_SSL)
    ngx_ssl_connection_t  *ssl;
#endif

    struct sockaddr    *local_sockaddr;//�����ĵ�ַ��c->local_sockaddr = ls->sockaddr;

    ngx_buf_t          *buffer;//����client_header_buffer_size��С�Ļ��������������տͻ��˵���������

    ngx_atomic_uint_t   number;

    ngx_uint_t          requests;//������ӵ��˶��ٴ�������ngx_http_init_request���ã���1

    unsigned            buffered:8;

    unsigned            log_error:3;     /* ngx_connection_log_error_e */

    unsigned            single_connection:1;
    unsigned            unexpected_eof:1;
    unsigned            timedout:1;
    unsigned            error:1;
    unsigned            destroyed:1;

    unsigned            idle:1;
    unsigned            close:1;

    unsigned            sendfile:1;
    unsigned            sndlowat:1;
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */

#if (NGX_HAVE_IOCP)
    unsigned            accept_context_updated:1;
#endif

#if (NGX_HAVE_AIO_SENDFILE)
    unsigned            aio_sendfile:1;
    ngx_buf_t          *busy_sendfile;
#endif

#if (NGX_THREADS)
    ngx_atomic_t        lock;
#endif
};


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, void *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);


#endif /* _NGX_CONNECTION_H_INCLUDED_ */
