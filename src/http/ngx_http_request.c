
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_init_request(ngx_event_t *ev);
static void ngx_http_process_request_line(ngx_event_t *rev);
static void ngx_http_process_request_headers(ngx_event_t *rev);
static ssize_t ngx_http_read_request_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_alloc_large_header_buffer(ngx_http_request_t *r,
    ngx_uint_t request_line);

static ngx_int_t ngx_http_process_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_unique_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_host(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_connection(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_user_agent(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_cookie(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);

static ngx_int_t ngx_http_process_request_header(ngx_http_request_t *r);
static void ngx_http_process_request(ngx_http_request_t *r);
static ssize_t ngx_http_validate_host(ngx_http_request_t *r, u_char **host,
    size_t len, ngx_uint_t alloc);
static ngx_int_t ngx_http_find_virtual_server(ngx_http_request_t *r,
    u_char *host, size_t len);

static void ngx_http_request_handler(ngx_event_t *ev);
static void ngx_http_terminate_request(ngx_http_request_t *r, ngx_int_t rc);
static void ngx_http_terminate_handler(ngx_http_request_t *r);
static void ngx_http_finalize_connection(ngx_http_request_t *r);
static ngx_int_t ngx_http_set_write_handler(ngx_http_request_t *r);
static void ngx_http_writer(ngx_http_request_t *r);
static void ngx_http_request_finalizer(ngx_http_request_t *r);

static void ngx_http_set_keepalive(ngx_http_request_t *r);
static void ngx_http_keepalive_handler(ngx_event_t *ev);
static void ngx_http_set_lingering_close(ngx_http_request_t *r);
static void ngx_http_lingering_close_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_post_action(ngx_http_request_t *r);
static void ngx_http_close_request(ngx_http_request_t *r, ngx_int_t error);
static void ngx_http_free_request(ngx_http_request_t *r, ngx_int_t error);
static void ngx_http_log_request(ngx_http_request_t *r);
static void ngx_http_close_connection(ngx_connection_t *c);

static u_char *ngx_http_log_error(ngx_log_t *log, u_char *buf, size_t len);
static u_char *ngx_http_log_error_handler(ngx_http_request_t *r,
    ngx_http_request_t *sr, u_char *buf, size_t len);

#if (NGX_HTTP_SSL)
static void ngx_http_ssl_handshake(ngx_event_t *rev);
static void ngx_http_ssl_handshake_handler(ngx_connection_t *c);
#endif


static char *ngx_http_client_errors[] = {

    /* NGX_HTTP_PARSE_INVALID_METHOD */
    "client sent invalid method",

    /* NGX_HTTP_PARSE_INVALID_REQUEST */
    "client sent invalid request",

    /* NGX_HTTP_PARSE_INVALID_09_METHOD */
    "client sent invalid method in HTTP/0.9 request"
};


ngx_http_header_t  ngx_http_headers_in[] = {
    { ngx_string("Host"), offsetof(ngx_http_headers_in_t, host),
                 ngx_http_process_host },//检查一下格式是不是合法的主机域，设置到header_in.server主机名字字段去，但不更改配置等

    { ngx_string("Connection"), offsetof(ngx_http_headers_in_t, connection),
                 ngx_http_process_connection },

    { ngx_string("If-Modified-Since"),
                 offsetof(ngx_http_headers_in_t, if_modified_since),
                 ngx_http_process_unique_header_line },//只能设置一次

    { ngx_string("User-Agent"), offsetof(ngx_http_headers_in_t, user_agent),
                 ngx_http_process_user_agent },

    { ngx_string("Referer"), offsetof(ngx_http_headers_in_t, referer),
                 ngx_http_process_header_line },

    { ngx_string("Content-Length"),
                 offsetof(ngx_http_headers_in_t, content_length),
                 ngx_http_process_unique_header_line },//只能唯一出现

    { ngx_string("Content-Type"),
                 offsetof(ngx_http_headers_in_t, content_type),
                 ngx_http_process_header_line },

    { ngx_string("Range"), offsetof(ngx_http_headers_in_t, range),
                 ngx_http_process_header_line },

    { ngx_string("If-Range"),
                 offsetof(ngx_http_headers_in_t, if_range),
                 ngx_http_process_unique_header_line },

    { ngx_string("Transfer-Encoding"),
                 offsetof(ngx_http_headers_in_t, transfer_encoding),
                 ngx_http_process_header_line },

    { ngx_string("Expect"),
                 offsetof(ngx_http_headers_in_t, expect),
                 ngx_http_process_unique_header_line },

#if (NGX_HTTP_GZIP)
    { ngx_string("Accept-Encoding"),
                 offsetof(ngx_http_headers_in_t, accept_encoding),
                 ngx_http_process_header_line },

    { ngx_string("Via"), offsetof(ngx_http_headers_in_t, via),
                 ngx_http_process_header_line },
#endif

    { ngx_string("Authorization"),
                 offsetof(ngx_http_headers_in_t, authorization),
                 ngx_http_process_unique_header_line },

    { ngx_string("Keep-Alive"), offsetof(ngx_http_headers_in_t, keep_alive),
                 ngx_http_process_header_line },

#if (NGX_HTTP_PROXY || NGX_HTTP_REALIP || NGX_HTTP_GEO)
    { ngx_string("X-Forwarded-For"),
                 offsetof(ngx_http_headers_in_t, x_forwarded_for),
                 ngx_http_process_header_line },
#endif

#if (NGX_HTTP_REALIP)
    { ngx_string("X-Real-IP"),
                 offsetof(ngx_http_headers_in_t, x_real_ip),
                 ngx_http_process_header_line },
#endif

#if (NGX_HTTP_HEADERS)
    { ngx_string("Accept"), offsetof(ngx_http_headers_in_t, accept),
                 ngx_http_process_header_line },

    { ngx_string("Accept-Language"),
                 offsetof(ngx_http_headers_in_t, accept_language),
                 ngx_http_process_header_line },
#endif

#if (NGX_HTTP_DAV)
    { ngx_string("Depth"), offsetof(ngx_http_headers_in_t, depth),
                 ngx_http_process_header_line },

    { ngx_string("Destination"), offsetof(ngx_http_headers_in_t, destination),
                 ngx_http_process_header_line },

    { ngx_string("Overwrite"), offsetof(ngx_http_headers_in_t, overwrite),
                 ngx_http_process_header_line },

    { ngx_string("Date"), offsetof(ngx_http_headers_in_t, date),
                 ngx_http_process_header_line },
#endif

    { ngx_string("Cookie"), 0, ngx_http_process_cookie },

    { ngx_null_string, 0, NULL }
};

// ngx_http_block 里面调用了 ngx_http_optimize_servers ，这个函数对listening和connection相关的变量进行了初始化和调优，
//并最终在 ngx_http_add_listening （被ngx_http_add_listening调用） 中注册了listening 的 handler 为 ngx_http_init_connection
void ngx_http_init_connection(ngx_connection_t *c)
{//注册这个新连接的回调函数，因为我是HTTP，因此对这个刚accept的连接，我需要注册我需要的读写事件回调。
//之前已经加入读写事件到EPOLL了的,在ngx_event_accept里面，就是一个监听端口有新连接事件，调用ngx_event_accept，
//然后它ACCEPT一个或多个连接，然后调用这个监听listening结构的handler,然后我们想到，在HTTP服务的时候，也就是ngx_http_block这个cmd会知道的
//我的连接，我要自己init，也就是ngx_http_init_connection的回调到listening->handler
    ngx_event_t         *rev;
    ngx_http_log_ctx_t  *ctx;

    ctx = ngx_palloc(c->pool, sizeof(ngx_http_log_ctx_t));
    if (ctx == NULL) {
        ngx_http_close_connection(c);
        return;
    }

    ctx->connection = c;//一个连接一个日志结构
    ctx->request = NULL;
    ctx->current_request = NULL;

    c->log->connection = c->number;
    c->log->handler = ngx_http_log_error;//错误日志打印的句柄，自定义，专门用来打HTTP的日志
    c->log->data = ctx;
    c->log->action = "reading client request line";//标注我正在干啥事，打日志时好用

    c->log_error = NGX_ERROR_INFO;

    rev = c->read;//读事件结构
    rev->handler = ngx_http_init_request;//如果待会有可读事件，那应该调用的handler为ngx_http_init_request，表示刚开始读数据时的状态
    c->write->handler = ngx_http_empty_handler;//空操作

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_reading, 1);
#endif

    if (rev->ready) {
//如果设置了TCP_DEFER_ACCEPT,那说明accept的时候，实际上数据已经到来.内核此时才通知我们有新连接，其实是还有数据
        /* the deferred accept(), rtsig, aio, iocp */
        if (ngx_use_accept_mutex) {//如果用了锁，那这里先不读了，挂到后面，退出返回后再读取。
            ngx_post_event(rev, &ngx_posted_events);
//把这个事件放到后面进行处理，相当于accept的时候，因为有accept锁，我们已经拿到锁了，所以这里先不读了，后续再读
            return;
        }
        ngx_http_init_request(rev);//果断去读数据
        return;
    }
    ngx_add_timer(rev, c->listening->post_accept_timeout);//等于client_header_timeout，就是客户端发送头部的延迟超时时间
    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
//如果没在epoll里面，加入。实际上在ngx_event_accept已经调用了ngx_epoll_add_connection
#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_reading, -1);
#endif
        ngx_http_close_connection(c);
        return;
    }
}

/*1. 初始化一个HTTP请求的相关结构，重点是ngx_http_request_t结构，然后读取请求第一个数据包的数据，并设置相关的回调函数
2. 设置rev->handler = ngx_http_process_request_line; ， 读事件的回调函数。
3.初始化数据后，调用读数据回调函数回调rev->handler(rev);//ngx_http_process_request_line。
相当于在ACCEPT一个连接后，先设置读数据回调函数为ngx_http_init_request，然后这里面其实就是想第一次嘛，初始化一个请求的相关数据，
然后立马将读数据回调函数归正，设置为正常的读HTTP读请求函数ngx_http_process_request_line
*/
static void ngx_http_init_request(ngx_event_t *rev)
{
    ngx_time_t                 *tp;
    ngx_uint_t                  i;
    ngx_connection_t           *c;
    ngx_http_request_t         *r;
    struct sockaddr_in         *sin;
    ngx_http_port_t            *port;
    ngx_http_in_addr_t         *addr;
    ngx_http_log_ctx_t         *ctx;
    ngx_http_addr_conf_t       *addr_conf;
    ngx_http_connection_t      *hc;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_core_main_conf_t  *cmcf;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6        *sin6;
    ngx_http_in6_addr_t        *addr6;
#endif

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_reading, -1);
#endif

    c = rev->data;//读事件结构中得到对应的客户端连接

    if (rev->timedout) {//在ngx_event_expire_timers可能在检查超时的定时器的时候，我这个连接超时了，然后就调用handler通知我了
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        ngx_http_close_connection(c);
        return;
    }
    c->requests++;//递增这个连接结构接受了多少请求复用
    hc = c->data;//不为空时，不指向下一个空闲的连接，而是用来指向ngx_http_connection_t。ngx_get_connection会清空此字段
    if (hc == NULL) {//如果这个连接复用了，就不为空了吧
        hc = ngx_pcalloc(c->pool, sizeof(ngx_http_connection_t));
        if (hc == NULL) {
            ngx_http_close_connection(c);
            return;
        }
    }
    r = hc->request;//初始时为空，如果复用了那倒不一定
    if (r) {//如果已经存在，表示是复用了，清空一下
        ngx_memzero(r, sizeof(ngx_http_request_t));
        r->pipeline = hc->pipeline;//拷贝一下pipeline

        if (hc->nbusy) {
            r->header_in = hc->busy[0];
        }

    } else {
        r = ngx_pcalloc(c->pool, sizeof(ngx_http_request_t));//申请一个巨大的HTTP结构体，里面啥都有
        if (r == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        hc->request = r;
    }

    c->data = r;//掉换一下指向，SOCK连接结构的data指向ngx_http_request_t
    r->http_connection = hc;//ngx_http_request_t的http_connection索引所属的HTTP连接。那怎么从这里找到对应的SOCK连接 ?r->connection = c;

    c->sent = 0;
    r->signature = NGX_HTTP_MODULE;
    /* find the server configuration for the address:port */
    port = c->listening->servers;//找到这个连接对应的监听端口对应的servers
    r->connection = c;//索引所对应的SOCK连接

    if (port->naddrs > 1) {//如果有多个地址
        /*
         * there are several addresses on this port and one of them
         * is an "*:port" wildcard so getsockname() in ngx_http_server_addr()
         * is required to determine a server address
         */
        if (ngx_connection_local_sockaddr(c, NULL, 0) != NGX_OK) {//通过这个函数，调用了getsockname，获得该连接的服务器端ip 
            ngx_http_close_connection(c);
            return;
        }
        switch (c->local_sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
//````
#endif
        default: /* AF_INET */
            sin = (struct sockaddr_in *) c->local_sockaddr;//这个连接的服务端IP
            addr = port->addrs;
            /* the last address is "*" */
            for (i = 0; i < port->naddrs - 1; i++) {
                if (addr[i].addr == sin->sin_addr.s_addr) {//寻找一个地址等于这个连接所对应的监听SOCKT的服务端地址，啥意思
                    break;
                }
            }
            addr_conf = &addr[i].conf;//使用这个地址的配置
            break;
        }

    } else {//只有一个servers，应该是就一个虚拟主机
        switch (c->local_sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
//···
#endif
        default: /* AF_INET */
            addr = port->addrs;//就一个
            addr_conf = &addr[0].conf;//直接取这个的配置
            break;
        }
    }
//就这么解析虚拟主机? 台疏忽了吧，虚拟主机到底在哪里解析的
    r->virtual_names = addr_conf->virtual_names;
    /* the default server configuration for the address:port */
    cscf = addr_conf->default_server;
//下面就是拿到这个连接对应的配置结构
    r->main_conf = cscf->ctx->main_conf;//指向对应的配置结构
    r->srv_conf = cscf->ctx->srv_conf;
    r->loc_conf = cscf->ctx->loc_conf;

    rev->handler = ngx_http_process_request_line;//设置这个SOCK的下一次可读事件的句柄
    r->read_event_handler = ngx_http_block_reading;

#if (NGX_HTTP_SSL)
    {
    ngx_http_ssl_srv_conf_t  *sscf;
    sscf = ngx_http_get_module_srv_conf(r, ngx_http_ssl_module);
    if (sscf->enable || addr_conf->ssl) {
        if (c->ssl == NULL) {
            c->log->action = "SSL handshaking";
            if (addr_conf->ssl && sscf->ssl.ctx == NULL) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,  "no \"ssl_certificate\" is defined in server listening on SSL port");
                ngx_http_close_connection(c);
                return;
            }
            if (ngx_ssl_create_connection(&sscf->ssl, c, NGX_SSL_BUFFER) != NGX_OK) {
                ngx_http_close_connection(c);
                return;
            }
            rev->handler = ngx_http_ssl_handshake;
        }
        r->main_filter_need_in_memory = 1;
    }
    }
#endif

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);//就是 (r)->loc_conf[module.ctx_index]
    c->log->file = clcf->error_log->file;//记录要打印的日志文件
    if (!(c->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {
        c->log->log_level = clcf->error_log->log_level;
    }

    if (c->buffer == NULL) {//设置用来接收客户端请求数据的缓冲区，可配置client_header_buffer_size
        c->buffer = ngx_create_temp_buf(c->pool,  cscf->client_header_buffer_size);
        if (c->buffer == NULL) {
            ngx_http_close_connection(c);
            return;
        }
    }

    if (r->header_in == NULL) {
        r->header_in = c->buffer;//指向这个连接
    }

    r->pool = ngx_create_pool(cscf->request_pool_size, c->log);
    if (r->pool == NULL) {
        ngx_http_close_connection(c);
        return;
    }
	
    if (ngx_list_init(&r->headers_out.headers, r->pool, 20, sizeof(ngx_table_elt_t)) != NGX_OK)
    {//初始化返回的HEADERS结构
        ngx_destroy_pool(r->pool);
        ngx_http_close_connection(c);
        return;
    }

    r->ctx = ngx_pcalloc(r->pool, sizeof(void *) * ngx_http_max_module);
    if (r->ctx == NULL) {
        ngx_destroy_pool(r->pool);
        ngx_http_close_connection(c);
        return;
    }
    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);//等于(r)->main_conf[module.ctx_index]
    r->variables = ngx_pcalloc(r->pool, cmcf->variables.nelts * sizeof(ngx_http_variable_value_t));
    if (r->variables == NULL) {//分配各种变量的结构体，就是配置里面的那些变量
        ngx_destroy_pool(r->pool);
        ngx_http_close_connection(c);
        return;
    }

    c->single_connection = 1;
    c->destroyed = 0;

    r->main = r;
    r->count = 1;

    tp = ngx_timeofday();//ngx_cached_time
    r->start_sec = tp->sec;
    r->start_msec = tp->msec;

    r->method = NGX_HTTP_UNKNOWN;
//初始化HTTP 头数据
    r->headers_in.content_length_n = -1;
    r->headers_in.keep_alive_n = -1;
    r->headers_out.content_length_n = -1;
    r->headers_out.last_modified_time = -1;

    r->uri_changes = NGX_HTTP_MAX_URI_CHANGES + 1;
    r->subrequests = NGX_HTTP_MAX_SUBREQUESTS + 1;

    r->http_state = NGX_HTTP_READING_REQUEST_STATE;//设置状态，初始读取请求状态

    ctx = c->log->data;//设置日志相关的结构
    ctx->request = r;
    ctx->current_request = r;
    r->log_handler = ngx_http_log_error_handler;

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_reading, 1);
    r->stat_reading = 1;
    (void) ngx_atomic_fetch_add(ngx_stat_requests, 1);
#endif
//可以看出ngx_http_init_request之所以在ngx_http_init_connection里面会设置为读事件回调函数，就是想再可读事件到来的时候，再开始初始化相关数据
//免得一开始接受一个连接就分配数据，太费了。然后初始化后到正常状态，设置为ngx_http_process_request_line
    rev->handler(rev);//ngx_http_process_request_line,上面刚刚设置的回调。
}


#if (NGX_HTTP_SSL)

static void
ngx_http_ssl_handshake(ngx_event_t *rev)
{
    u_char               buf[1];
    ssize_t              n;
    ngx_int_t            rc;
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    c = rev->data;
    r = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http check ssl handshake");
    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;
        ngx_http_close_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }
    n = recv(c->fd, (char *) buf, 1, MSG_PEEK);
    if (n == -1 && ngx_socket_errno == NGX_EAGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, c->listening->post_accept_timeout);
        }
        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
        return;
    }

    if (n == 1) {
        if (buf[0] & 0x80 /* SSLv2 */ || buf[0] == 0x16 /* SSLv3/TLSv1 */) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, rev->log, 0,  "https ssl handshake: 0x%02Xd", buf[0]);
            rc = ngx_ssl_handshake(c);
            if (rc == NGX_AGAIN) {
                if (!rev->timer_set) {
                    ngx_add_timer(rev, c->listening->post_accept_timeout);
                }
                c->ssl->handler = ngx_http_ssl_handshake_handler;
                return;
            }
            ngx_http_ssl_handshake_handler(c);
            return;
        } else {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "plain http");
            r->plain_http = 1;
        }
    }

    c->log->action = "reading client request line";

    rev->handler = ngx_http_process_request_line;
    ngx_http_process_request_line(rev);
}


static void
ngx_http_ssl_handshake_handler(ngx_connection_t *c)
{
    ngx_http_request_t  *r;

    if (c->ssl->handshaked) {

        /*
         * The majority of browsers do not send the "close notify" alert.
         * Among them are MSIE, old Mozilla, Netscape 4, Konqueror,
         * and Links.  And what is more, MSIE ignores the server's alert.
         *
         * Opera and recent Mozilla send the alert.
         */

        c->ssl->no_wait_shutdown = 1;

        c->read->handler = ngx_http_process_request_line;
        /* STUB: epoll edge */ c->write->handler = ngx_http_empty_handler;

        ngx_http_process_request_line(c->read);

        return;
    }

    r = c->data;

    ngx_http_close_request(r, NGX_HTTP_BAD_REQUEST);

    return;
}

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME

int
ngx_http_ssl_servername(ngx_ssl_conn_t *ssl_conn, int *ad, void *arg)
{
    size_t                    len;
    u_char                   *host;
    const char               *servername;
    ngx_connection_t         *c;
    ngx_http_request_t       *r;
    ngx_http_ssl_srv_conf_t  *sscf;

    servername = SSL_get_servername(ssl_conn, TLSEXT_NAMETYPE_host_name);

    if (servername == NULL) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    c = ngx_ssl_get_connection(ssl_conn);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "SSL server name: \"%s\"", servername);

    len = ngx_strlen(servername);

    if (len == 0) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    r = c->data;

    host = (u_char *) servername;

    len = ngx_http_validate_host(r, &host, len, 1);

    if (len <= 0) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    if (ngx_http_find_virtual_server(r, host, len) != NGX_OK) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    sscf = ngx_http_get_module_srv_conf(r, ngx_http_ssl_module);

    SSL_set_SSL_CTX(ssl_conn, sscf->ssl.ctx);

    return SSL_TLSEXT_ERR_OK;
}

#endif

#endif


static void ngx_http_process_request_line(ngx_event_t *rev)
{//读取客户端发送的第一行数据，也就是GET /UII HTTP 1.1 , 读取完毕后，会调用ngx_http_process_request_headers读取头部数据
//ngx_event_t的data记录所属的连接connection_t，连接里面目前指向http_request_t
    u_char                    *host;
    ssize_t                    n;
    ngx_int_t                  rc, rv;
    ngx_connection_t          *c;
    ngx_http_request_t        *r;
    ngx_http_core_srv_conf_t  *cscf;

    c = rev->data;//在ngx_http_init_request里面设置的
    r = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http process request line");
    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;
        ngx_http_close_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    rc = NGX_AGAIN;
    for ( ;; ) {
        if (rc == NGX_AGAIN) {
            n = ngx_http_read_request_header(r);//读HTTP头部，返回读到的数据的大小。或者失败
            if (n == NGX_AGAIN || n == NGX_ERROR) {
                return;
            }
        }
        rc = ngx_http_parse_request_line(r, r->header_in);//解析请求的第一行，也就是: GET /index.html HTTP 1.1
        if (rc == NGX_OK) {
            /* the request line has been parsed successfully */
            r->request_line.len = r->request_end - r->request_start;//记录请求头的缓冲位置
            r->request_line.data = r->request_start;
            if (r->args_start) {//如果有?开始的部分，就是后面的参数
                r->uri.len = r->args_start - 1 - r->uri_start;
            } else {
                r->uri.len = r->uri_end - r->uri_start;
            }
            if (r->complex_uri || r->quoted_uri) {//URI上有.%#/等符号，就定义为复杂的URI
                r->uri.data = ngx_pnalloc(r->pool, r->uri.len + 1);
                if (r->uri.data == NULL) {
                    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }
				//解析参数部分,因为含有复杂的字符. 解析结果放入uri.data里面
                cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
                rc = ngx_http_parse_complex_uri(r, cscf->merge_slashes);//解析请求头的参数部分等。
                if (rc == NGX_HTTP_PARSE_INVALID_REQUEST) {
                    ngx_log_error(NGX_LOG_INFO, c->log, 0,  "client sent invalid request");
                    ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
                    return;
                }
            } else {
                r->uri.data = r->uri_start;//都是正常字符，直接改变指向就行。内存也不用释放了。
            }

            r->unparsed_uri.len = r->uri_end - r->uri_start;
            r->unparsed_uri.data = r->uri_start;
            r->valid_unparsed_uri = r->space_in_uri ? 0 : 1;
            r->method_name.len = r->method_end - r->request_start + 1;//得到请求的方法。GET/POST
            r->method_name.data = r->request_line.data;//r->request_line.data = r->request_start;

            if (r->http_protocol.data) {
                r->http_protocol.len = r->request_end - r->http_protocol.data;
            }
            if (r->uri_ext) {//uri里面的最后部分文件后缀名，会用来做默认content-type处理
                if (r->args_start) {
                    r->exten.len = r->args_start - 1 - r->uri_ext;
                } else {
                    r->exten.len = r->uri_end - r->uri_ext;
                }
                r->exten.data = r->uri_ext;
            }
			//?参数处理
            if (r->args_start && r->uri_end > r->args_start) {
                r->args.len = r->uri_end - r->args_start;
                r->args.data = r->args_start;
            }
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,  "http request line: \"%V\"", &r->request_line);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http uri: \"%V\"", &r->uri);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http args: \"%V\"", &r->args);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http exten: \"%V\"", &r->exten);
			//HOST处理
            if (r->host_start && r->host_end) {
			//请注意，请求行是可能带host等全URL的，比如:  GET http://www.w3.org/pub/WWW/TheProject.html HTTP/1.1
                host = r->host_start;//设置了HOST，验证一下是否合法，结果放入host 临时变量
                n = ngx_http_validate_host(r, &host,  r->host_end - r->host_start, 0);
                if (n == 0) {
                    ngx_log_error(NGX_LOG_INFO, c->log, 0, "client sent invalid host in request line");
                    ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
                    return;
                }
                if (n < 0) {
                    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }
                r->headers_in.server.len = n;
                r->headers_in.server.data = host;//保存一下HOST字符串
            }
            if (r->http_version < NGX_HTTP_VERSION_10) {//HTTP 1.0版本
     		/*http://blog.csdn.net/forgotaboutgirl/article/details/6936982
     		在HTTP1.0中认为每台服务器都绑定一个唯一的IP地址，因此，请求消息中的URL并没有传递主机名（hostname）。
     		但随着虚拟主机技术的发展，在一台物理服务器上可以存在多个虚拟主机（Multi-homed Web Servers），并且它们共享一个IP地址。
			HTTP1.1的请求消息和响应消息都应支持Host头域，且请求消息中如果没有Host头域会报告一个错误（400 Bad Request）。
			此外，服务器应该接受以绝对路径标记的资源请求。*/
                if (ngx_http_find_virtual_server(r, r->headers_in.server.data, r->headers_in.server.len) == NGX_ERROR)
                {//找出对应的虚拟主机。那好吧，如果到后面看到Host的HTTP HEADER，还会再换一下，再找的。
                    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }
                ngx_http_process_request(r);//1.0就直接处理了。啥意思
                return;
            }


            if (ngx_list_init(&r->headers_in.headers, r->pool, 20, sizeof(ngx_table_elt_t)) != NGX_OK){
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            if (ngx_array_init(&r->headers_in.cookies, r->pool, 2, sizeof(ngx_table_elt_t *))  != NGX_OK){
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            c->log->action = "reading client request headers";
            rev->handler = ngx_http_process_request_headers;
            ngx_http_process_request_headers(rev);//下面去读取请求的头部数据，第一行的GET在此读取成功。下面读取头部数据了。

            return;
        }

        if (rc != NGX_AGAIN) {//上面是处理请求头的GET /index.html时过来的，在这里如果不是AGAIN，说明失败了，那就出错关闭连接
            /* there was error while a request line parsing */
            ngx_log_error(NGX_LOG_INFO, c->log, 0, ngx_http_client_errors[rc - NGX_HTTP_CLIENT_ERROR]);
            ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
            return;
        }
		//到这里，说明rc=AGAIN,请求头解析未完成
        /* NGX_AGAIN: a request line parsing is still incomplete */
        if (r->header_in->pos == r->header_in->end) {
            rv = ngx_http_alloc_large_header_buffer(r, 1);
            if (rv == NGX_ERROR) {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            if (rv == NGX_DECLINED) {
                r->request_line.len = r->header_in->end - r->request_start;
                r->request_line.data = r->request_start;
                ngx_log_error(NGX_LOG_INFO, c->log, 0, "client sent too long URI");
                ngx_http_finalize_request(r, NGX_HTTP_REQUEST_URI_TOO_LARGE);
                return;
            }
        }
    }
}


static void ngx_http_process_request_headers(ngx_event_t *rev)
{//ngx_http_process_request_line调用这里，此时已经读取完了请求的第一行GET /uri http 1.0.
//下面开始循环读取请求的头部headers数据
    u_char                     *p;
    size_t                      len;
    ssize_t                     n;
    ngx_int_t                   rc, rv;
    ngx_table_elt_t            *h;
    ngx_connection_t           *c;
    ngx_http_header_t          *hh;
    ngx_http_request_t         *r;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_core_main_conf_t  *cmcf;

    c = rev->data;//从可读事件结构中得到对应的HTTP连接
    r = c->data;//然后得到连接对应的数据结构，对于HTTP，就是ngx_http_request_t
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http process request header line");

    if (rev->timedout) {//超时是永恒的话题
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;//待会打日志时用，标记为超时
        ngx_http_close_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }
	//在ngx_http_process_request_line处理请求的GET/POST里面，会根据HOST域设置对应的虚拟主机数据配置
    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
    rc = NGX_AGAIN;

    for ( ;; ) {
        if (rc == NGX_AGAIN) {
            if (r->header_in->pos == r->header_in->end) {//如果header_in缓冲里面没有数据结构了，那就得去读一点了。不然就读一点，处理一点。
                rv = ngx_http_alloc_large_header_buffer(r, 0);//申请一块大的缓冲区，保留之前的数据。后面的0表示现在不是在处理请求行的过程中
                if (rv == NGX_ERROR) {
                    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }
                if (rv == NGX_DECLINED) {//头部数据大小超过配额了，拒绝
                    p = r->header_name_start;
                    if (p == NULL) {
                        ngx_log_error(NGX_LOG_INFO, c->log, 0,  "client sent too large request");
                        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
                        return;
                    }
                    len = r->header_in->end - p;
                    if (len > NGX_MAX_ERROR_STR - 300) {
                        len = NGX_MAX_ERROR_STR - 300;
                        p[len++] = '.'; p[len++] = '.'; p[len++] = '.';
                    }
                    ngx_log_error(NGX_LOG_INFO, c->log, 0, "client sent too long header line: \"%*s\"", len, r->header_name_start);
                    ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
                    return;
                }
            }
			
            n = ngx_http_read_request_header(r);//尝试去读一点数据出来，这个函数负责读取get行和header数据。
            if (n == NGX_AGAIN || n == NGX_ERROR) {
                return;//如果没有读数据了，或者失败了。都会设置相应的错误码的
            }
        }
		//有数据放在header_in了，下面进行处理解析header行，每次只解析一行。GET/POST行已经在ngx_http_parse_request_line进行处理了。
        rc = ngx_http_parse_header_line(r, r->header_in, cscf->underscores_in_headers);
        if (rc == NGX_OK) {//解析出了一行。下面需要将这个HEADER放入哈希表中，然后调用对应的ngx_http_headers_in里面的回调。
            if (r->invalid_header && cscf->ignore_invalid_headers) {
                /* there was error while a header line parsing */
                ngx_log_error(NGX_LOG_INFO, c->log, 0, "client sent invalid header line: \"%*s\"", r->header_end - r->header_name_start, r->header_name_start);
                continue;
            }
            /* a header line has been parsed successfully */
            h = ngx_list_push(&r->headers_in.headers);//在r->headers_in.headers里面申请一个位置，用来放置请求头部
            if (h == NULL) {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
			//下面就拿到了一对K-V了。
            h->hash = r->header_hash;
            h->key.len = r->header_name_end - r->header_name_start;//header的名字长度
            h->key.data = r->header_name_start;//记录名字开始。
            h->key.data[h->key.len] = '\0';//标志结尾
            h->value.len = r->header_end - r->header_start;
            h->value.data = r->header_start;
            h->value.data[h->value.len] = '\0';

            h->lowcase_key = ngx_pnalloc(r->pool, h->key.len);
            if (h->lowcase_key == NULL) {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }

            if (h->key.len == r->lowcase_index) {
                ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);

            } else {
                ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
            }
			//找到这个请求头部对应的处理函数，调用之。这个哈希表在ngx_http_headers_in里面
			//只是简单设置一下变量，并未进行实际的操作，处理。实际处理在后面的ngx_http_process_request_header
            hh = ngx_hash_find(&cmcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
            if (hh && hh->handler(r, h, hh->offset) != NGX_OK) {
                return;
            }
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http header: \"%V: %V\"", &h->key, &h->value);
            continue;//继续下一个请求头部
        }
        if (rc == NGX_HTTP_PARSE_HEADER_DONE) {//全部请求的HEADER已经处理完毕，碰到了空行
            /* a whole header has been parsed successfully */
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http header done");
            r->request_length += r->header_in->pos - r->header_in->start;
            r->http_state = NGX_HTTP_PROCESS_REQUEST_STATE;//下一个步骤，处理请求状态。全部状态在这里:ngx_http_state_e
            rc = ngx_http_process_request_header(r);//简单处理一下HEADER域，设置虚拟主机等。
            if (rc != NGX_OK) {
                return;
            }
            ngx_http_process_request(r);//然后进入请求处理间断，里面会进入ngx_http_handler->phrases。
            return;
        }

        if (rc == NGX_AGAIN) {
            /* a header line parsing is still not complete */
            continue;
        }
        /* rc == NGX_HTTP_PARSE_INVALID_HEADER: "\r" is not followed by "\n" */
        ngx_log_error(NGX_LOG_INFO, c->log, 0,  "client sent invalid header line: \"%*s\\r...\"",
                      r->header_end - r->header_name_start, r->header_name_start);
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return;
    }
}


static ssize_t ngx_http_read_request_header(ngx_http_request_t *r)
{//看看有没有数据在header_in的缓冲区，如果有，则返回大小，否则读一些，返回大小
//读取第一行GET、POST的时候会调用这里获取数据，读取header的时候也会调用这里。
    ssize_t                    n;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_srv_conf_t  *cscf;

    c = r->connection;//根据http请求结构，得到所绑定的连接，然后得到连接的read读事件结构体。
    rev = c->read;
    n = r->header_in->last - r->header_in->pos;//看看buf里面是否还有数据，如果有，返回数据长度
    if (n > 0) {
        return n;
    }
    if (rev->ready) {
//在ngx_event_accept接受一个连接的时候设置的读事件回调，写事件回调,函数列表在ngx_os_io结构里面
//        c->recv = ngx_recv;//k ngx_unix_recv  ，其实还有ngx_ssl_recv
        n = c->recv(c, r->header_in->last, r->header_in->end - r->header_in->last);
    } else {//什么情况下会这样�?一个连接没有准备好 .ngx_unix_recv里面设置过为0，表示没有数据可以读了
        n = NGX_AGAIN;//暂时没有可读数据，待会可能有读的
    }

    if (n == NGX_AGAIN) {//如果刚才没有读到数据
        if (!rev->timer_set) {
            cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
            ngx_add_timer(rev, cscf->client_header_timeout);
//设置一个读去header头的超时定时器，如果超时了，就会调用ngx_http_process_request_line进行读取头部，然后一开始就失败超时了的
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {//刚才没读到，现在就加入可读事件epoll中
            ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_ERROR;
        }

        return NGX_AGAIN;
    }

    if (n == 0) {//如果recv返回0，表示连接被中断了。
        ngx_log_error(NGX_LOG_INFO, c->log, 0, "client closed prematurely connection");
    }

    if (n == 0 || n == NGX_ERROR) {
        c->error = 1;//有错误发生，关闭连接
        c->log->action = "reading client request headers";
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return NGX_ERROR;
    }
    r->header_in->last += n;//缓冲区往后增加n个字符。返回数据大小
    return n;
}


static ngx_int_t
ngx_http_alloc_large_header_buffer(ngx_http_request_t *r, ngx_uint_t request_line)
{/* equest_line表示是不是在处理请求行的GET/POST /index.html的时候。如果是，那么一堆对应的指针也需要负责拷贝一下。否则不需要。
     * 因为nginx中，所有的请求头的保存形式都是指针（起始和结束地址）， 
     * 所以一行完整的请求头必须放在连续的内存块中。如果旧的缓冲区不能 
     * 再放下整行请求头，则分配新缓冲区，并从旧缓冲区拷贝已经读取的部分请求头， 
     * 拷贝完之后，需要修改所有相关指针指向到新缓冲区。 
     * status为0表示解析完一行请求头之后，缓冲区正好被用完，这种情况不需要拷贝 
     */  
    u_char                    *old, *new;
    ngx_buf_t                 *b;
    ngx_http_connection_t     *hc;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http alloc large header buffer");
    if (request_line && r->state == 0) {//如果可以复用，并且当前请求状态为某个状态的初始部分
        /* the client fills up the buffer with "\r\n" */
	 /*在解析请求行阶段，如果客户端在发送请求行之前发送了大量回车换行符将缓冲区塞满了，针对这种情况，nginx只是简单的重置缓冲区，丢弃这些垃圾 
     * 数据，不需要分配更大的内存。 */  
        r->request_length += r->header_in->end - r->header_in->start;
        r->header_in->pos = r->header_in->start;
        r->header_in->last = r->header_in->start;
        return NGX_OK;
    }
    old = request_line ? r->request_start : r->header_name_start;
    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
    if (r->state != 0 && (size_t) (r->header_in->pos - old) >= cscf->large_client_header_buffers.size) {
        return NGX_DECLINED;//大小超过配额了，拒绝
    }
    hc = r->http_connection;
    if (hc->nfree) { /*首先在ngx_http_connection_t结构中查找是否有空闲缓冲区，有的话，直接取之 */
        b = hc->free[--hc->nfree];//取一个空闲的用
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http large header free: %p %uz",  b->pos, b->end - b->last);
    } else if (hc->nbusy < cscf->large_client_header_buffers.num) { /* 检查给该请求分配的请求头缓冲区个数是否已经超过限制，默认最大个数为4个 */  
        if (hc->busy == NULL) { /* 如果还没有达到最大分配数量，则分配一个新的大缓冲区 */ 
            hc->busy = ngx_palloc(r->connection->pool, cscf->large_client_header_buffers.num * sizeof(ngx_buf_t *));
            if (hc->busy == NULL) {
                return NGX_ERROR;
            }
        }
        b = ngx_create_temp_buf(r->connection->pool, cscf->large_client_header_buffers.size);
        if (b == NULL) {
            return NGX_ERROR;
        }
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http large header alloc: %p %uz", b->pos, b->end - b->last);
    } else {
        return NGX_DECLINED;
    }
    hc->busy[hc->nbusy++] = b;//用缓冲区结构busy索引新分配的缓冲区
    if (r->state == 0) {//为0表示刚刚开始。也就是没有之前的数据包袱。
        /*status为0表示解析完一行请求头之后，缓冲区正好被用完，这种情况不需要拷贝r->header_name_start开始的数据。前面的数据已经有指针指向了的。
         * r->state == 0 means that a header line was parsed successfully
         * and we do not need to copy incomplete header line and
         * to relocate the parser header pointers
         */
        r->request_length += r->header_in->end - r->header_in->start;
        r->header_in = b;//切换缓冲区为新的。因为我们没有旧数据的包袱
        return NGX_OK;
    }
	//否则旧的数据需要处理
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http large header copy: %d", r->header_in->pos - old);
    r->request_length += old - r->header_in->start;//当前的数据长度
    new = b->start;
    ngx_memcpy(new, old, r->header_in->pos - old);//拷贝旧的数据.如果不是请求头行，则只需要拷贝
    b->pos = new + (r->header_in->pos - old);
    b->last = new + (r->header_in->pos - old);
    if (request_line) {//如果是请求头，那还需要拷贝这下面的一坨指针，如果是在解析请求的HEADER阶段，则不需要。2个地方调用了这里。
        r->request_start = new;
        if (r->request_end) {
            r->request_end = new + (r->request_end - old);
        }
        r->method_end = new + (r->method_end - old);
        r->uri_start = new + (r->uri_start - old);
        r->uri_end = new + (r->uri_end - old);
        if (r->schema_start) {
            r->schema_start = new + (r->schema_start - old);
            r->schema_end = new + (r->schema_end - old);
        }
        if (r->host_start) {
            r->host_start = new + (r->host_start - old);
            if (r->host_end) {
                r->host_end = new + (r->host_end - old);
            }
        }
        if (r->port_start) {
            r->port_start = new + (r->port_start - old);
            r->port_end = new + (r->port_end - old);
        }
        if (r->uri_ext) {
            r->uri_ext = new + (r->uri_ext - old);
        }
        if (r->args_start) {
            r->args_start = new + (r->args_start - old);
        }
        if (r->http_protocol.data) {
            r->http_protocol.data = new + (r->http_protocol.data - old);
        }
    } else {//如果是在处理HEADER的过程中，那就只需要移动一下header_相关的结构。也就是请求的头部字段结构
        r->header_name_start = new;
        r->header_name_end = new + (r->header_name_end - old);
        r->header_start = new + (r->header_start - old);
        r->header_end = new + (r->header_end - old);
    }
    r->header_in = b;
    return NGX_OK;
}


static ngx_int_t
ngx_http_process_header_line(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{//简单设置一下变了，如果已经设置了，就忽略
    ngx_table_elt_t  **ph;
    ph = (ngx_table_elt_t **) ((char *) &r->headers_in + offset);
    if (*ph == NULL) {
        *ph = h;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_process_unique_header_line(ngx_http_request_t *r, ngx_table_elt_t *h, ngx_uint_t offset)
{//检查一下所设置的值是否已经设置过。只能设置i一次
    ngx_table_elt_t  **ph;
    ph = (ngx_table_elt_t **) ((char *) &r->headers_in + offset);
    if (*ph == NULL) {
        *ph = h;
        return NGX_OK;
    }
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "client sent duplicate header line: \"%V: %V\", previous value: \"%V: %V\"",
                  &h->key, &h->value, &(*ph)->key, &(*ph)->value);

    ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_process_host(ngx_http_request_t *r, ngx_table_elt_t *h, ngx_uint_t offset)
{//解析一下请求头的格式，将HOST值设置到headers_in.server里面
    u_char   *host;
    ssize_t   len;

    if (r->headers_in.host == NULL) {
        r->headers_in.host = h;
    }
    host = h->value.data;
    len = ngx_http_validate_host(r, &host, h->value.len, 0);//解析一下请求头的格式。
    if (len == 0) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,  "client sent invalid host header");
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return NGX_ERROR;
    }
    if (len < 0) {
        ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_ERROR;
    }

    if (r->headers_in.server.len) {
        return NGX_OK;
    }
	//如果拿到了请求头部的HOST，为啥这里不进行虚拟主机的选择呢?
    r->headers_in.server.len = len;
    r->headers_in.server.data = host;
    return NGX_OK;
}


static ngx_int_t
ngx_http_process_connection(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    if (ngx_strcasestrn(h->value.data, "close", 5 - 1)) {
        r->headers_in.connection_type = NGX_HTTP_CONNECTION_CLOSE;

    } else if (ngx_strcasestrn(h->value.data, "keep-alive", 10 - 1)) {
        r->headers_in.connection_type = NGX_HTTP_CONNECTION_KEEP_ALIVE;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_process_user_agent(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char  *user_agent, *msie;
    if (r->headers_in.user_agent) {
        return NGX_OK;//只要第一个
    }
    r->headers_in.user_agent = h;
    /* check some widespread browsers while the header is in CPU cache */
    user_agent = h->value.data;
    msie = ngx_strstrn(user_agent, "MSIE ", 5 - 1);
    if (msie && msie + 7 < user_agent + h->value.len) {
        r->headers_in.msie = 1;
        if (msie[6] == '.') {
            switch (msie[5]) {
            case '4':
                r->headers_in.msie4 = 1;
                /* fall through */
            case '5':
                r->headers_in.msie6 = 1;
                break;
            case '6':
                if (ngx_strstrn(msie + 8, "SV1", 3 - 1) == NULL) {
                    r->headers_in.msie6 = 1;
                }
                break;
            }
        }
#if 0
        /* MSIE ignores the SSL "close notify" alert */
        if (c->ssl) {
            c->ssl->no_send_shutdown = 1;
        }
#endif
    }
    if (ngx_strstrn(user_agent, "Opera", 5 - 1)) {
        r->headers_in.opera = 1;
        r->headers_in.msie = 0;
        r->headers_in.msie4 = 0;
        r->headers_in.msie6 = 0;
    }
    if (!r->headers_in.msie && !r->headers_in.opera) {
        if (ngx_strstrn(user_agent, "Gecko/", 6 - 1)) {
            r->headers_in.gecko = 1;

        } else if (ngx_strstrn(user_agent, "Chrome/", 7 - 1)) {
            r->headers_in.chrome = 1;

        } else if (ngx_strstrn(user_agent, "Safari/", 7 - 1)) {
            r->headers_in.safari = 1;

        } else if (ngx_strstrn(user_agent, "Konqueror", 9 - 1)) {
            r->headers_in.konqueror = 1;
        }
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_process_cookie(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  **cookie;
    cookie = ngx_array_push(&r->headers_in.cookies);
    if (cookie) {
        *cookie = h;
        return NGX_OK;
    }
    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_process_request_header(ngx_http_request_t *r)
{//ngx_http_process_request_headers调用这里，读取完毕了所有的头部数据，已经碰到了\n\r。但没有读取body还
//对HEADER头部域进行简单处理，解析虚拟主机，请求长度等。调用这里之后，会调用ngx_http_process_request进行请求的处理。
    if (ngx_http_find_virtual_server(r, r->headers_in.server.data, r->headers_in.server.len) == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_ERROR;
    }

    if (r->headers_in.host == NULL && r->http_version > NGX_HTTP_VERSION_10) {//HTTP 1.0不允许不设置host,因为1.0不支持虚拟主机
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "client sent HTTP/1.1 request without \"Host\" header");
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return NGX_ERROR;
    }

    if (r->headers_in.content_length) {//如果设置了内容长度,得到内容长度大小
        r->headers_in.content_length_n = ngx_atoof(r->headers_in.content_length->value.data, r->headers_in.content_length->value.len);
        if (r->headers_in.content_length_n == NGX_ERROR) {
            ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "client sent invalid \"Content-Length\" header");
            ngx_http_finalize_request(r, NGX_HTTP_LENGTH_REQUIRED);
            return NGX_ERROR;
        }
    }

    if (r->method & NGX_HTTP_PUT && r->headers_in.content_length_n == -1) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "client sent %V method without \"Content-Length\" header", &r->method_name);
        ngx_http_finalize_request(r, NGX_HTTP_LENGTH_REQUIRED);
        return NGX_ERROR;
    }

    if (r->method & NGX_HTTP_TRACE) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,"client sent TRACE method");
        ngx_http_finalize_request(r, NGX_HTTP_NOT_ALLOWED);
        return NGX_ERROR;
    }

    if (r->headers_in.transfer_encoding && ngx_strcasestrn(r->headers_in.transfer_encoding->value.data, "chunked", 7 - 1)) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "client sent \"Transfer-Encoding: chunked\" header");
        ngx_http_finalize_request(r, NGX_HTTP_LENGTH_REQUIRED);
        return NGX_ERROR;
    }

    if (r->headers_in.connection_type == NGX_HTTP_CONNECTION_KEEP_ALIVE) {
        if (r->headers_in.keep_alive) {
            r->headers_in.keep_alive_n = ngx_atotm(r->headers_in.keep_alive->value.data, r->headers_in.keep_alive->value.len);
        }
    }
    return NGX_OK;
}


static void
ngx_http_process_request(ngx_http_request_t *r)
{//ngx_http_process_request_headers函数调用这里，目前情景我们回顾一下：读取完毕了请求行，headers，并且查找到了虚拟主机，
//设置好了相关srv/loc_conf。下面就是进行真正请求的处理啦。
    ngx_connection_t  *c;
    c = r->connection;//拿到当前请求的连接结构
    if (r->plain_http) {//是否通过SSL发送明文请求。如果是，关闭连接
        ngx_log_error(NGX_LOG_INFO, c->log, 0, "client sent plain HTTP request to HTTPS port");
        ngx_http_finalize_request(r, NGX_HTTP_TO_HTTPS);
        return;
    }
#if (NGX_HTTP_SSL)
    if (c->ssl) {//如果是ssl，进行一些跟加密相关的处理，具体不详。
        long                      rc;
        X509                     *cert;
        ngx_http_ssl_srv_conf_t  *sscf;
        sscf = ngx_http_get_module_srv_conf(r, ngx_http_ssl_module);
        if (sscf->verify) {
            rc = SSL_get_verify_result(c->ssl->connection);
            if (rc != X509_V_OK) {
                ngx_log_error(NGX_LOG_INFO,c->log,0,"client SSL certificate verify error: (%l:%s)",rc,X509_verify_cert_error_string(rc));
                ngx_ssl_remove_cached_session(sscf->ssl.ctx, (SSL_get0_session(c->ssl->connection)));
                ngx_http_finalize_request(r, NGX_HTTPS_CERT_ERROR);
                return;
            }
            if (sscf->verify == 1) {
                cert = SSL_get_peer_certificate(c->ssl->connection);
                if (cert == NULL) {
                    ngx_log_error(NGX_LOG_INFO, c->log, 0, "client sent no required SSL certificate");
                    ngx_ssl_remove_cached_session(sscf->ssl.ctx, (SSL_get0_session(c->ssl->connection)));
                    ngx_http_finalize_request(r, NGX_HTTPS_NO_CERT);
                    return;
                }
                X509_free(cert);
            }
        }
    }
#endif
    if (c->read->timer_set) {//这是干嘛，关闭定时器吗?
        ngx_del_timer(c->read);
    }
#if (NGX_STAT_STUB)//如果编译的时候设置了STUB，择这里需要增加统计计数
    (void) ngx_atomic_fetch_add(ngx_stat_reading, -1);
    r->stat_reading = 0;
    (void) ngx_atomic_fetch_add(ngx_stat_writing, 1);
    r->stat_writing = 1;
#endif
    c->read->handler = ngx_http_request_handler;//设置这个连接的读事件结构。有可读事件就调动这个函数
    c->write->handler = ngx_http_request_handler;
    r->read_event_handler = ngx_http_block_reading;//

    ngx_http_handler(r);//请求的头部数据已经读取处理完了，准备长袍，进行头部解析，重定向，
    //以及content phrase，这个时候会触发ngx_http_fastcgi_handler等内容处理模块，
    //其里面会调用ngx_http_read_client_request_body->ngx_http_upstream_init从而进入FCGI的处理阶段或者proxy处理阶段。
    ngx_http_run_posted_requests(c);//ngx_http_run_posted_requests函数是处理子请求的。是么
}


static ssize_t
 ngx_http_validate_host(ngx_http_request_t *r, u_char **host, size_t len, ngx_uint_t alloc)
{// 解析HOST头返回头部长度
    u_char      *h, ch;
    size_t       i, last;
    ngx_uint_t   dot;

    last = len;
    h = *host;
    dot = 0;
    for (i = 0; i < len; i++) {
        ch = h[i];
        if (ch == '.') {
            if (dot) {//刚也是点号，又一个点号，不行
                return 0;
            }
            dot = 1;//好吧，我遇到了一个点号了哈。下面继续
            continue;
        }
        dot = 0;//不是点号了
        if (ch == ':') {//OK，就到这了
            last = i;
            continue;
        }
        if (ngx_path_separator(ch) || ch == '\0') {
            return 0;//host上面别给我来这个了吧
        }
        if (ch >= 'A' || ch < 'Z') {
            alloc = 1;//OK, 正常字符
        }
    }
    if (dot) {//最后一个竟然是点号。退一个
        last--;
    }
    if (alloc) {
        *host = ngx_pnalloc(r->pool, last) ;
        if (*host == NULL) {
            return -1;
        }
        ngx_strlow(*host, h, last);
    }
    return last;
}


static ngx_int_t
ngx_http_find_virtual_server(ngx_http_request_t *r, u_char *host, size_t len)
{//找到对应host的虚拟主机名字，并将虚拟主机对应的配置设置到当前请求结构里面
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;
    if (r->virtual_names == NULL) {
        return NGX_DECLINED;
    }
    cscf = ngx_hash_find_combined(&r->virtual_names->names, ngx_hash_key(host, len), host, len);
    if (cscf) {//上面在r->virtual_names->names的哈希表里面找到虚拟主机名字
        goto found;
    }
#if (NGX_PCRE)
    if (len && r->virtual_names->nregex) {
        ngx_int_t                n;
        ngx_uint_t               i;
        ngx_str_t                name;
        ngx_http_server_name_t  *sn;

        name.len = len;
        name.data = host;

        sn = r->virtual_names->regex;

        for (i = 0; i < r->virtual_names->nregex; i++) {
            n = ngx_http_regex_exec(r, sn[i].regex, &name);
            if (n == NGX_OK) {//正则表达式匹配成功
                cscf = sn[i].server;
                goto found;
            }
            if (n == NGX_DECLINED) {
                continue;
            }
            return NGX_ERROR;
        }
    }
#endif
    return NGX_OK;
found:
    r->srv_conf = cscf->ctx->srv_conf;//替换配置
    r->loc_conf = cscf->ctx->loc_conf;//替换loc配置
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    r->connection->log->file = clcf->error_log->file;//日志也换一下。那从这里就可以看出，一个HTTP连接的日志不一定是打在对应的vhost的日志文件里面。得等到解析HTTP头部之后才能知道。
    if (!(r->connection->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {
        r->connection->log->log_level = clcf->error_log->log_level;
    }
    return NGX_OK;
}


static void
ngx_http_request_handler(ngx_event_t *ev)
{//一个连接到HTTP阶段后，设置的读写事件回调都是这个。有可读，可写事件都调用这里进行分流。
//ngx_http_process_request设置这个。
    ngx_connection_t    *c;
    ngx_http_request_t  *r;
    ngx_http_log_ctx_t  *ctx;

    c = ev->data;
    r = c->data;

    ctx = c->log->data;
    ctx->current_request = r;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http run request: \"%V?%V\"", &r->uri, &r->args);

    if (ev->write) {
        r->write_event_handler(r);

    } else {
        r->read_event_handler(r);//HTTP设置为ngx_http_read_client_request_body_handler，也就是读取POST数据阶段
    }

    ngx_http_run_posted_requests(c);
}


void
ngx_http_run_posted_requests(ngx_connection_t *c)
{//跑一下挂起的请求。在哪里设置的呢
    ngx_http_request_t         *r;
    ngx_http_log_ctx_t         *ctx;
    ngx_http_posted_request_t  *pr;
    for ( ;; ) {
        if (c->destroyed) {
            return;
        }
        r = c->data;
        pr = r->main->posted_requests;//不断的遍历挂起的子请求，一个个执行他们。
        if (pr == NULL) {
            return;
        }
        r->main->posted_requests = pr->next;
        r = pr->request;
        ctx = c->log->data;
        ctx->current_request = r;
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "http posted request: \"%V?%V\"", &r->uri, &r->args);
        r->write_event_handler(r);
    }
}


ngx_int_t
ngx_http_post_request(ngx_http_request_t *r, ngx_http_posted_request_t *pr)
{
    ngx_http_posted_request_t  **p;

    if (pr == NULL) {
        pr = ngx_palloc(r->pool, sizeof(ngx_http_posted_request_t));
        if (pr == NULL) {
            return NGX_ERROR;
        }
    }

    pr->request = r;
    pr->next = NULL;

    for (p = &r->main->posted_requests; *p; p = &(*p)->next) { /* void */ }

    *p = pr;

    return NGX_OK;
}


void
ngx_http_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_connection_t          *c;
    ngx_http_request_t        *pr;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;

    ngx_log_debug5(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http finalize request: %d, \"%V?%V\" a:%d, c:%d",
                   rc, &r->uri, &r->args, r == c->data, r->main->count);

    if (rc == NGX_DONE) {
        ngx_http_finalize_connection(r);
        return;
    }

    if (rc == NGX_OK && r->filter_finalize) {
        c->error = 1;
        return;
    }

    if (rc == NGX_DECLINED) {
        r->content_handler = NULL;
        r->write_event_handler = ngx_http_core_run_phases;
        ngx_http_core_run_phases(r);
        return;
    }

    if (r != r->main && r->post_subrequest) {
        rc = r->post_subrequest->handler(r, r->post_subrequest->data, rc);
    }

    if (rc == NGX_ERROR
        || rc == NGX_HTTP_REQUEST_TIME_OUT
        || rc == NGX_HTTP_CLIENT_CLOSED_REQUEST
        || c->error)
    {
        if (ngx_http_post_action(r) == NGX_OK) {
            return;
        }

        if (r->main->blocked) {
            r->write_event_handler = ngx_http_request_finalizer;
        }

        ngx_http_terminate_request(r, rc);
        return;
    }

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE
        || rc == NGX_HTTP_CREATED
        || rc == NGX_HTTP_NO_CONTENT)
    {
        if (rc == NGX_HTTP_CLOSE) {
            ngx_http_terminate_request(r, rc);
            return;
        }

        if (r == r->main) {
            if (c->read->timer_set) {
                ngx_del_timer(c->read);
            }

            if (c->write->timer_set) {
                ngx_del_timer(c->write);
            }
        }

        c->read->handler = ngx_http_request_handler;
        c->write->handler = ngx_http_request_handler;

        ngx_http_finalize_request(r, ngx_http_special_response_handler(r, rc));
        return;
    }

    if (r != r->main) {

        if (r->buffered || r->postponed) {

            if (ngx_http_set_write_handler(r) != NGX_OK) {
                ngx_http_terminate_request(r, 0);
            }

            return;
        }

#if (NGX_DEBUG)
        if (r != c->data) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http finalize non-active request: \"%V?%V\"",
                           &r->uri, &r->args);
        }
#endif

        pr = r->parent;

        if (r == c->data) {

            r->main->count--;

            if (!r->logged) {

                clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

                if (clcf->log_subrequest) {
                    ngx_http_log_request(r);
                }

                r->logged = 1;

            } else {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                              "subrequest: \"%V?%V\" logged again",
                              &r->uri, &r->args);
            }

            r->done = 1;

            if (pr->postponed && pr->postponed->request == r) {
                pr->postponed = pr->postponed->next;
            }

            c->data = pr;

        } else {

            r->write_event_handler = ngx_http_request_finalizer;

            if (r->waited) {
                r->done = 1;
            }
        }

        if (ngx_http_post_request(pr, NULL) != NGX_OK) {
            r->main->count++;
            ngx_http_terminate_request(r, 0);
            return;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                       "http wake parent request: \"%V?%V\"",
                       &pr->uri, &pr->args);

        return;
    }

    if (r->buffered || c->buffered || r->postponed || r->blocked) {

        if (ngx_http_set_write_handler(r) != NGX_OK) {
            ngx_http_terminate_request(r, 0);
        }

        return;
    }

    if (r != c->data) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "http finalize non-active request: \"%V?%V\"",
                      &r->uri, &r->args);
        return;
    }

    r->done = 1;
    r->write_event_handler = ngx_http_request_empty_handler;

    if (!r->post_action) {
        r->request_complete = 1;
    }

    if (ngx_http_post_action(r) == NGX_OK) {
        return;
    }

    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }

    if (c->write->timer_set) {
        c->write->delayed = 0;
        ngx_del_timer(c->write);
    }

    if (c->read->eof) {
        ngx_http_close_request(r, 0);
        return;
    }

    ngx_http_finalize_connection(r);
}


static void
ngx_http_terminate_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_http_cleanup_t    *cln;
    ngx_http_request_t    *mr;
    ngx_http_ephemeral_t  *e;

    mr = r->main;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http terminate request count:%d", mr->count);

    if (rc > 0 && (mr->headers_out.status == 0 || mr->connection->sent == 0)) {
        mr->headers_out.status = rc;
    }

    cln = mr->cleanup;
    mr->cleanup = NULL;

    while (cln) {
        if (cln->handler) {
            cln->handler(cln->data);
        }

        cln = cln->next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http terminate cleanup count:%d blk:%d",
                   mr->count, mr->blocked);

    if (mr->write_event_handler) {

        if (mr->blocked) {
            return;
        }

        e = ngx_http_ephemeral(mr);
        mr->posted_requests = NULL;
        mr->write_event_handler = ngx_http_terminate_handler;
        (void) ngx_http_post_request(mr, &e->terminal_posted_request);
        return;
    }

    ngx_http_close_request(mr, rc);
}


static void
ngx_http_terminate_handler(ngx_http_request_t *r)
{
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http terminate handler count:%d", r->count);

    r->count = 1;

    ngx_http_close_request(r, 0);
}


static void
ngx_http_finalize_connection(ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (r->main->count != 1) {

        if (r->discard_body) {
            r->read_event_handler = ngx_http_discarded_request_body_handler;

            if (r->lingering_time == 0) {
                r->lingering_time = ngx_time()
                                      + (time_t) (clcf->lingering_time / 1000);
                ngx_add_timer(r->connection->read, clcf->lingering_timeout);
            }
        }

        ngx_http_close_request(r, 0);
        return;
    }

    if (!ngx_terminate
         && !ngx_exiting
         && r->keepalive
         && clcf->keepalive_timeout > 0)
    {
        ngx_http_set_keepalive(r);
        return;

    } else if (r->lingering_close && clcf->lingering_timeout > 0) {
/*关于lingering_close,http://tengine.taobao.org/book/chapter_2.html
lingering_close
lingering_close，字面意思就是延迟关闭，也就是说，当nginx要关闭连接时，并非立即关闭连接，而是再等待一段时间后才真正关掉连接。
为什么要这样呢？我们先来看看这样一个场景。nginx在接收客户端的请求时，可能由于客户端或服务端出错了，要立即响应错误信息给客户端，
而nginx在响应错误信息后，大分部情况下是需要关闭当前连接。如果客户端正在发送数据，或数据还没有到达服务端，服务端就将连接关掉了。
那么，客户端发送的数据会收到RST包，此时，客户端对于接收到的服务端的数据，将不会发送ACK，也就是说，
客户端将不会拿到服务端发送过来的错误信息数据。那客户端肯定会想，这服务器好霸道，动不动就reset我的连接，连个错误信息都没有。

在上面这个场景中，我们可以看到，关键点是服务端给客户端发送了RST包，导致自己发送的数据在客户端忽略掉了。
所以，解决问题的重点是，让服务端别发RST包。再想想，我们发送RST是因为我们关掉了连接，关掉连接是因为我们不想再处理此连接了，
也不会有任何数据产生了。对于全双工的TCP连接来说，我们只需要关掉写就行了，读可以继续进行，我们只需要丢掉读到的任何数据就行了，
这样的话，当我们关掉连接后，客户端再发过来的数据，就不会再收到RST了。当然最终我们还是需要关掉这个读端的，所以我们会设置一个超时时间，
在这个时间过后，就关掉读，客户端再发送数据来就不管了，作为服务端我会认为，都这么长时间了，发给你的错误信息也应该读到了，
再慢就不关我事了，要怪就怪你RP不好了。当然，正常的客户端，在读取到数据后，会关掉连接，此时服务端就会在超时时间内关掉读端。
这些正是lingering_close所做的事情。协议栈提供 SO_LINGER 这个选项，它的一种配置情况就是来处理lingering_close的情况的，
不过nginx是自己实现的lingering_close。lingering_close存在的意义就是来读取剩下的客户端发来的数据，所以nginx会有一个读超时时间，
通过lingering_timeout选项来设置，如果在lingering_timeout时间内还没有收到数据，则直接关掉连接。nginx还支持设置一个总的读取时间，
通过lingering_time来设置，这个时间也就是nginx在关闭写之后，保留socket的时间，客户端需要在这个时间内发送完所有的数据，
否则nginx在这个时间过后，会直接关掉连接。当然，nginx是支持配置是否打开lingering_close选项的，通过lingering_close选项来配置。 
那么，我们在实际应用中，是否应该打开lingering_close呢？这个就没有固定的推荐值了，如Maxim Dounin所说，
lingering_close的主要作用是保持更好的客户端兼容性，但是却需要消耗更多的额外资源（比如连接会一直占着）。
*/
        ngx_http_set_lingering_close(r);
        return;
    }

    ngx_http_close_request(r, 0);
}


static ngx_int_t
ngx_http_set_write_handler(ngx_http_request_t *r)
{
    ngx_event_t               *wev;
    ngx_http_core_loc_conf_t  *clcf;

    r->http_state = NGX_HTTP_WRITING_REQUEST_STATE;

    r->read_event_handler = r->discard_body ?
                                ngx_http_discarded_request_body_handler:
                                ngx_http_test_reading;
    r->write_event_handler = ngx_http_writer;

    wev = r->connection->write;

    if (wev->ready && wev->delayed) {
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (!wev->delayed) {
        ngx_add_timer(wev, clcf->send_timeout);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
        ngx_http_close_request(r, 0);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_writer(ngx_http_request_t *r)
{
    int                        rc;
    ngx_event_t               *wev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    wev = c->write;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                   "http writer handler: \"%V?%V\"", &r->uri, &r->args);

    clcf = ngx_http_get_module_loc_conf(r->main, ngx_http_core_module);

    if (wev->timedout) {
        if (!wev->delayed) {
            ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                          "client timed out");
            c->timedout = 1;

            ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
            return;
        }

        wev->timedout = 0;
        wev->delayed = 0;

        if (!wev->ready) {
            ngx_add_timer(wev, clcf->send_timeout);

            if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
                ngx_http_close_request(r, 0);
            }

            return;
        }

    } else {
        if (wev->delayed || r->aio) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                           "http writer delayed");

            if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
                ngx_http_close_request(r, 0);
            }

            return;
        }
    }

    rc = ngx_http_output_filter(r, NULL);

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http writer output filter: %d, \"%V?%V\"",
                   rc, &r->uri, &r->args);

    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, rc);
        return;
    }

    if (r->buffered || r->postponed || (r == r->main && c->buffered)) {

        if (!wev->ready && !wev->delayed) {
            ngx_add_timer(wev, clcf->send_timeout);
        }

        if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }

        return;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                   "http writer done: \"%V?%V\"", &r->uri, &r->args);

    r->write_event_handler = ngx_http_request_empty_handler;

    ngx_http_finalize_request(r, rc);
}


static void
ngx_http_request_finalizer(ngx_http_request_t *r)
{
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,  "http finalizer done: \"%V?%V\"", &r->uri, &r->args);

    ngx_http_finalize_request(r, 0);
}


void
ngx_http_block_reading(ngx_http_request_t *r)
{//删除连接的读事件注册，不关注读事件了。
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http reading blocked");
    /* aio does not call this handler */
    if ((ngx_event_flags & NGX_USE_LEVEL_EVENT)  && r->connection->read->active)
    {
        if (ngx_del_event(r->connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }
    }
}


void
ngx_http_test_reading(ngx_http_request_t *r)
{
    int                n;
    char               buf[1];
    ngx_err_t          err;
    ngx_event_t       *rev;
    ngx_connection_t  *c;

    c = r->connection;
    rev = c->read;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http test reading");

#if (NGX_HAVE_KQUEUE)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {

        if (!rev->pending_eof) {
            return;
        }

        rev->eof = 1;
        c->error = 1;
        err = rev->kq_errno;

        goto closed;
    }

#endif

    n = recv(c->fd, buf, 1, MSG_PEEK);

    if (n == 0) {
        rev->eof = 1;
        c->error = 1;
        err = 0;

        goto closed;

    } else if (n == -1) {
        err = ngx_socket_errno;

        if (err != NGX_EAGAIN) {
            rev->eof = 1;
            c->error = 1;

            goto closed;
        }
    }

    /* aio does not call this handler */

    if ((ngx_event_flags & NGX_USE_LEVEL_EVENT) && rev->active) {

        if (ngx_del_event(rev, NGX_READ_EVENT, 0) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }
    }

    return;

closed:

    if (err) {
        rev->error = 1;
    }

    ngx_log_error(NGX_LOG_INFO, c->log, err,
                  "client closed prematurely connection");

    ngx_http_finalize_request(r, 0);
}


static void
ngx_http_set_keepalive(ngx_http_request_t *r)
{
    int                        tcp_nodelay;
    ngx_int_t                  i;
    ngx_buf_t                 *b, *f;
    ngx_event_t               *rev, *wev;
    ngx_connection_t          *c;
    ngx_http_connection_t     *hc;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rev = c->read;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "set http keepalive handler");

    if (r->discard_body) {
        r->write_event_handler = ngx_http_request_empty_handler;
        r->lingering_time = ngx_time() + (time_t) (clcf->lingering_time / 1000);
        ngx_add_timer(rev, clcf->lingering_timeout);
        return;
    }

    c->log->action = "closing request";

    hc = r->http_connection;
    b = r->header_in;

    if (b->pos < b->last) {

        /* the pipelined request */

        if (b != c->buffer) {

            /*
             * If the large header buffers were allocated while the previous
             * request processing then we do not use c->buffer for
             * the pipelined request (see ngx_http_init_request()).
             *
             * Now we would move the large header buffers to the free list.
             */

            cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

            if (hc->free == NULL) {
                hc->free = ngx_palloc(c->pool,
                  cscf->large_client_header_buffers.num * sizeof(ngx_buf_t *));

                if (hc->free == NULL) {
                    ngx_http_close_request(r, 0);
                    return;
                }
            }

            for (i = 0; i < hc->nbusy - 1; i++) {
                f = hc->busy[i];
                hc->free[hc->nfree++] = f;
                f->pos = f->start;
                f->last = f->start;
            }

            hc->busy[0] = b;
            hc->nbusy = 1;
        }
    }

    r->keepalive = 0;

    ngx_http_free_request(r, 0);

    c->data = hc;

    ngx_add_timer(rev, clcf->keepalive_timeout);

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_http_close_connection(c);
        return;
    }

    wev = c->write;
    wev->handler = ngx_http_empty_handler;

    if (b->pos < b->last) {

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "pipelined request");

#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_reading, 1);
#endif

        hc->pipeline = 1;
        c->log->action = "reading client pipelined request line";

        rev->handler = ngx_http_init_request;
        ngx_post_event(rev, &ngx_posted_events);
        return;
    }

    hc->pipeline = 0;

    /*
     * To keep a memory footprint as small as possible for an idle
     * keepalive connection we try to free the ngx_http_request_t and
     * c->buffer's memory if they were allocated outside the c->pool.
     * The large header buffers are always allocated outside the c->pool and
     * are freed too.
     */

    if (ngx_pfree(c->pool, r) == NGX_OK) {
        hc->request = NULL;
    }

    b = c->buffer;

    if (ngx_pfree(c->pool, b->start) == NGX_OK) {

        /*
         * the special note for ngx_http_keepalive_handler() that
         * c->buffer's memory was freed
         */

        b->pos = NULL;

    } else {
        b->pos = b->start;
        b->last = b->start;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "hc free: %p %d",
                   hc->free, hc->nfree);

    if (hc->free) {
        for (i = 0; i < hc->nfree; i++) {
            ngx_pfree(c->pool, hc->free[i]->start);
            hc->free[i] = NULL;
        }

        hc->nfree = 0;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "hc busy: %p %d",
                   hc->busy, hc->nbusy);

    if (hc->busy) {
        for (i = 0; i < hc->nbusy; i++) {
            ngx_pfree(c->pool, hc->busy[i]->start);
            hc->busy[i] = NULL;
        }

        hc->nbusy = 0;
    }

#if (NGX_HTTP_SSL)
    if (c->ssl) {
        ngx_ssl_free_buffer(c);
    }
#endif

    rev->handler = ngx_http_keepalive_handler;

    if (wev->active && (ngx_event_flags & NGX_USE_LEVEL_EVENT)) {
        if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
            ngx_http_close_connection(c);
            return;
        }
    }

    c->log->action = "keepalive";

    if (c->tcp_nopush == NGX_TCP_NOPUSH_SET) {
        if (ngx_tcp_push(c->fd) == -1) {
            ngx_connection_error(c, ngx_socket_errno, ngx_tcp_push_n " failed");
            ngx_http_close_connection(c);
            return;
        }

        c->tcp_nopush = NGX_TCP_NOPUSH_UNSET;
        tcp_nodelay = ngx_tcp_nodelay_and_tcp_nopush ? 1 : 0;

    } else {
        tcp_nodelay = 1;
    }

    if (tcp_nodelay
        && clcf->tcp_nodelay
        && c->tcp_nodelay == NGX_TCP_NODELAY_UNSET)
    {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "tcp_nodelay");

        if (setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY,
                       (const void *) &tcp_nodelay, sizeof(int))
            == -1)
        {
#if (NGX_SOLARIS)
            /* Solaris returns EINVAL if a socket has been shut down */
            c->log_error = NGX_ERROR_IGNORE_EINVAL;
#endif

            ngx_connection_error(c, ngx_socket_errno,
                                 "setsockopt(TCP_NODELAY) failed");

            c->log_error = NGX_ERROR_INFO;
            ngx_http_close_connection(c);
            return;
        }

        c->tcp_nodelay = NGX_TCP_NODELAY_SET;
    }

#if 0
    /* if ngx_http_request_t was freed then we need some other place */
    r->http_state = NGX_HTTP_KEEPALIVE_STATE;
#endif

    c->idle = 1;

    if (rev->ready) {
        ngx_post_event(rev, &ngx_posted_events);
    }
}


static void
ngx_http_keepalive_handler(ngx_event_t *rev)
{
    size_t             size;
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_connection_t  *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http keepalive handler");

    if (rev->timedout || c->close) {
        ngx_http_close_connection(c);
        return;
    }

#if (NGX_HAVE_KQUEUE)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        if (rev->pending_eof) {
            c->log->handler = NULL;
            ngx_log_error(NGX_LOG_INFO, c->log, rev->kq_errno,
                          "kevent() reported that client %V closed "
                          "keepalive connection", &c->addr_text);
#if (NGX_HTTP_SSL)
            if (c->ssl) {
                c->ssl->no_send_shutdown = 1;
            }
#endif
            ngx_http_close_connection(c);
            return;
        }
    }

#endif

    b = c->buffer;
    size = b->end - b->start;

    if (b->pos == NULL) {

        /*
         * The c->buffer's memory was freed by ngx_http_set_keepalive().
         * However, the c->buffer->start and c->buffer->end were not changed
         * to keep the buffer size.
         */

        b->pos = ngx_palloc(c->pool, size);
        if (b->pos == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        b->start = b->pos;
        b->last = b->pos;
        b->end = b->pos + size;
    }

    /*
     * MSIE closes a keepalive connection with RST flag
     * so we ignore ECONNRESET here.
     */

    c->log_error = NGX_ERROR_IGNORE_ECONNRESET;
    ngx_set_socket_errno(0);

    n = c->recv(c, b->last, size);
    c->log_error = NGX_ERROR_INFO;

    if (n == NGX_AGAIN) {
        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_http_close_connection(c);
        }

        return;
    }

    if (n == NGX_ERROR) {
        ngx_http_close_connection(c);
        return;
    }

    c->log->handler = NULL;

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, ngx_socket_errno,
                      "client %V closed keepalive connection", &c->addr_text);
        ngx_http_close_connection(c);
        return;
    }

    b->last += n;

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_reading, 1);
#endif

    c->log->handler = ngx_http_log_error;
    c->log->action = "reading client request line";

    c->idle = 0;

    ngx_http_init_request(rev);
}


static void
ngx_http_set_lingering_close(ngx_http_request_t *r)
{
    ngx_event_t               *rev, *wev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    rev = c->read;
    rev->handler = ngx_http_lingering_close_handler;

    r->lingering_time = ngx_time() + (time_t) (clcf->lingering_time / 1000);
    ngx_add_timer(rev, clcf->lingering_timeout);

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_http_close_request(r, 0);
        return;
    }

    wev = c->write;
    wev->handler = ngx_http_empty_handler;

    if (wev->active && (ngx_event_flags & NGX_USE_LEVEL_EVENT)) {
        if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
            ngx_http_close_request(r, 0);
            return;
        }
    }

    if (ngx_shutdown_socket(c->fd, NGX_WRITE_SHUTDOWN) == -1) {
        ngx_connection_error(c, ngx_socket_errno,
                             ngx_shutdown_socket_n " failed");
        ngx_http_close_request(r, 0);
        return;
    }

    if (rev->ready) {
        ngx_http_lingering_close_handler(rev);
    }
}


static void
ngx_http_lingering_close_handler(ngx_event_t *rev)
{
    ssize_t                    n;
    ngx_msec_t                 timer;
    ngx_connection_t          *c;
    ngx_http_request_t        *r;
    ngx_http_core_loc_conf_t  *clcf;
    u_char                     buffer[NGX_HTTP_LINGERING_BUFFER_SIZE];

    c = rev->data;
    r = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http lingering close handler");

    if (rev->timedout) {
        c->timedout = 1;
        ngx_http_close_request(r, 0);
        return;
    }

    timer = (ngx_msec_t) (r->lingering_time - ngx_time());
    if (timer <= 0) {
        ngx_http_close_request(r, 0);
        return;
    }

    do {
        n = c->recv(c, buffer, NGX_HTTP_LINGERING_BUFFER_SIZE);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "lingering read: %d", n);

        if (n == NGX_ERROR || n == 0) {
            ngx_http_close_request(r, 0);
            return;
        }

    } while (rev->ready);

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_http_close_request(r, 0);
        return;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    timer *= 1000;

    if (timer > clcf->lingering_timeout) {
        timer = clcf->lingering_timeout;
    }

    ngx_add_timer(rev, timer);
}


void
ngx_http_empty_handler(ngx_event_t *wev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0, "http empty handler");

    return;
}


void
ngx_http_request_empty_handler(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http request empty handler");
    return;
}


ngx_int_t
ngx_http_send_special(ngx_http_request_t *r, ngx_uint_t flags)
{//刷一个空的缓冲区给客户端，实际上就是要手动刷一下，促使数据发送给客户端。
    ngx_buf_t    *b;
    ngx_chain_t   out;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }
    if (flags & NGX_HTTP_LAST) {
        if (r == r->main && !r->post_action) {
            b->last_buf = 1;
        } else {
            b->sync = 1;
            b->last_in_chain = 1;
        }
    }
    if (flags & NGX_HTTP_FLUSH) {
        b->flush = 1;
    }
    out.buf = b;
    out.next = NULL;
    return ngx_http_output_filter(r, &out);//刷一个空的缓冲区给客户端，实际上就是要手动刷一下，促使数据发送给客户端。
}


static ngx_int_t
ngx_http_post_action(ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->post_action.data == NULL) {
        return NGX_DECLINED;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "post action: \"%V\"", &clcf->post_action);

    r->main->count--;

    r->http_version = NGX_HTTP_VERSION_9;
    r->header_only = 1;
    r->post_action = 1;

    r->read_event_handler = ngx_http_block_reading;

    if (clcf->post_action.data[0] == '/') {
        ngx_http_internal_redirect(r, &clcf->post_action, NULL);

    } else {
        ngx_http_named_location(r, &clcf->post_action);
    }

    return NGX_OK;
}


static void
ngx_http_close_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_connection_t  *c;

    r = r->main;
    c = r->connection;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http request count:%d blk:%d", r->count, r->blocked);

    if (r->count == 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "http request count is zero");
    }

    r->count--;

    if (r->count || r->blocked) {
        return;
    }

    ngx_http_free_request(r, rc);
    ngx_http_close_connection(c);
}


static void
ngx_http_free_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_t                 *log;
    struct linger              linger;
    ngx_http_cleanup_t        *cln;
    ngx_http_log_ctx_t        *ctx;
    ngx_http_core_loc_conf_t  *clcf;

    log = r->connection->log;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "http close request");

    if (r->pool == NULL) {
        ngx_log_error(NGX_LOG_ALERT, log, 0, "http request already closed");
        return;
    }

    for (cln = r->cleanup; cln; cln = cln->next) {
        if (cln->handler) {
            cln->handler(cln->data);
        }
    }

#if (NGX_STAT_STUB)

    if (r->stat_reading) {
        (void) ngx_atomic_fetch_add(ngx_stat_reading, -1);
    }

    if (r->stat_writing) {
        (void) ngx_atomic_fetch_add(ngx_stat_writing, -1);
    }

#endif

    if (rc > 0 && (r->headers_out.status == 0 || r->connection->sent == 0)) {
        r->headers_out.status = rc;
    }

    log->action = "logging request";

    ngx_http_log_request(r);

    log->action = "closing request";

    if (r->connection->timedout) {
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        if (clcf->reset_timedout_connection) {
            linger.l_onoff = 1;
            linger.l_linger = 0;

            if (setsockopt(r->connection->fd, SOL_SOCKET, SO_LINGER,
                           (const void *) &linger, sizeof(struct linger)) == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                              "setsockopt(SO_LINGER) failed");
            }
        }
    }

    /* the various request strings were allocated from r->pool */
    ctx = log->data;
    ctx->request = NULL;

    r->request_line.len = 0;

    r->connection->destroyed = 1;

    ngx_destroy_pool(r->pool);
}


static void
ngx_http_log_request(ngx_http_request_t *r)
{
    ngx_uint_t                  i, n;
    ngx_http_handler_pt        *log_handler;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    log_handler = cmcf->phases[NGX_HTTP_LOG_PHASE].handlers.elts;
    n = cmcf->phases[NGX_HTTP_LOG_PHASE].handlers.nelts;

    for (i = 0; i < n; i++) {
        log_handler[i](r);
    }
}


static void
ngx_http_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

#if (NGX_HTTP_SSL)

    if (c->ssl) {
        if (ngx_ssl_shutdown(c) == NGX_AGAIN) {
            c->ssl->handler = ngx_http_close_connection;
            return;
        }
    }

#endif

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_active, -1);
#endif

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}


static u_char *
ngx_http_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    u_char              *p;
    ngx_http_request_t  *r;
    ngx_http_log_ctx_t  *ctx;

    if (log->action) {
        p = ngx_snprintf(buf, len, " while %s", log->action);
        len -= p - buf;
        buf = p;
    }

    ctx = log->data;

    p = ngx_snprintf(buf, len, ", client: %V", &ctx->connection->addr_text);
    len -= p - buf;

    r = ctx->request;

    if (r) {
        return r->log_handler(r, ctx->current_request, p, len);

    } else {
        p = ngx_snprintf(p, len, ", server: %V", &ctx->connection->listening->addr_text);
    }

    return p;
}


static u_char *
ngx_http_log_error_handler(ngx_http_request_t *r, ngx_http_request_t *sr,
    u_char *buf, size_t len)
{
    char                      *uri_separator;
    u_char                    *p;
    ngx_http_upstream_t       *u;
    ngx_http_core_srv_conf_t  *cscf;

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

    p = ngx_snprintf(buf, len, ", server: %V", &cscf->server_name);
    len -= p - buf;
    buf = p;

    if (r->request_line.data == NULL && r->request_start) {
        for (p = r->request_start; p < r->header_in->last; p++) {
            if (*p == CR || *p == LF) {
                break;
            }
        }

        r->request_line.len = p - r->request_start;
        r->request_line.data = r->request_start;
    }

    if (r->request_line.len) {
        p = ngx_snprintf(buf, len, ", request: \"%V\"", &r->request_line);
        len -= p - buf;
        buf = p;
    }

    if (r != sr) {
        p = ngx_snprintf(buf, len, ", subrequest: \"%V\"", &sr->uri);
        len -= p - buf;
        buf = p;
    }
    u = sr->upstream;
    if (u && u->peer.name) {
        uri_separator = "";

#if (NGX_HAVE_UNIX_DOMAIN)
        if (u->peer.sockaddr && u->peer.sockaddr->sa_family == AF_UNIX) {
            uri_separator = ":";
        }
#endif

        p = ngx_snprintf(buf, len, ", upstream: \"%V%V%s%V\"", &u->schema, u->peer.name, uri_separator, &u->uri);
        len -= p - buf;
        buf = p;
    }
    if (r->headers_in.host) {
        p = ngx_snprintf(buf, len, ", host: \"%V\"", &r->headers_in.host->value);
        len -= p - buf;
        buf = p;
    }
    if (r->headers_in.referer) {
        p = ngx_snprintf(buf, len, ", referrer: \"%V\"", &r->headers_in.referer->value);
        buf = p;
    }

    return buf;
}
