
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#if (NGX_HTTP_CACHE)
static ngx_int_t ngx_http_upstream_cache(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_cache_send(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_cache_status(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
#endif

static void ngx_http_upstream_init_request(ngx_http_request_t *r);
static void ngx_http_upstream_resolve_handler(ngx_resolver_ctx_t *ctx);
static void ngx_http_upstream_rd_check_broken_connection(ngx_http_request_t *r);
static void ngx_http_upstream_wr_check_broken_connection(ngx_http_request_t *r);
static void ngx_http_upstream_check_broken_connection(ngx_http_request_t *r,
    ngx_event_t *ev);
static void ngx_http_upstream_connect(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_reinit(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_send_request(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_send_request_handler(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_process_header(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_test_next(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_intercept_errors(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_test_connect(ngx_connection_t *c);
static ngx_int_t ngx_http_upstream_process_headers(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_process_body_in_memory(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_send_response(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void
    ngx_http_upstream_process_non_buffered_downstream(ngx_http_request_t *r);
static void
    ngx_http_upstream_process_non_buffered_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void
    ngx_http_upstream_process_non_buffered_request(ngx_http_request_t *r,
    ngx_uint_t do_write);
static ngx_int_t ngx_http_upstream_non_buffered_filter_init(void *data);
static ngx_int_t ngx_http_upstream_non_buffered_filter(void *data,
    ssize_t bytes);
static void ngx_http_upstream_process_downstream(ngx_http_request_t *r);
static void ngx_http_upstream_process_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_process_request(ngx_http_request_t *r);
static void ngx_http_upstream_store(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_dummy_handler(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_next(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_uint_t ft_type);
static void ngx_http_upstream_cleanup(void *data);
static void ngx_http_upstream_finalize_request(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_int_t rc);

static ngx_int_t ngx_http_upstream_process_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_set_cookie(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t
    ngx_http_upstream_process_cache_control(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_ignore_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_expires(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_accel_expires(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_limit_rate(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_buffering(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_charset(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t
    ngx_http_upstream_copy_multi_header_lines(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_content_type(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_content_length(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_last_modified(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_rewrite_location(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_rewrite_refresh(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_allow_ranges(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);

#if (NGX_HTTP_GZIP)
static ngx_int_t ngx_http_upstream_copy_content_encoding(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
#endif

static ngx_int_t ngx_http_upstream_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_upstream_addr_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_upstream_status_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_upstream_response_time_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_upstream_response_length_variable(
    ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);

static char *ngx_http_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);
static char *ngx_http_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_http_upstream_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_upstream_init_main_conf(ngx_conf_t *cf, void *conf);

#if (NGX_HTTP_SSL)
static void ngx_http_upstream_ssl_init_connection(ngx_http_request_t *,
    ngx_http_upstream_t *u, ngx_connection_t *c);
static void ngx_http_upstream_ssl_handshake(ngx_connection_t *c);
#endif


ngx_http_upstream_header_t  ngx_http_upstream_headers_in[] = {

    { ngx_string("Status"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, status),
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("Content-Type"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, content_type),
                 ngx_http_upstream_copy_content_type, 0, 1 },

    { ngx_string("Content-Length"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, content_length),
                 ngx_http_upstream_copy_content_length, 0, 0 },

    { ngx_string("Date"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, date),
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, date), 0 },

    { ngx_string("Last-Modified"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, last_modified),
                 ngx_http_upstream_copy_last_modified, 0, 0 },

    { ngx_string("ETag"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, etag),
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, etag), 0 },

    { ngx_string("Server"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, server),
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, server), 0 },

    { ngx_string("WWW-Authenticate"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, www_authenticate),
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("Location"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, location),
                 ngx_http_upstream_rewrite_location, 0, 0 },

    { ngx_string("Refresh"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_rewrite_refresh, 0, 0 },

    { ngx_string("Set-Cookie"),
                 ngx_http_upstream_process_set_cookie, 0,
                 ngx_http_upstream_copy_header_line, 0, 1 },

    { ngx_string("Content-Disposition"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_copy_header_line, 0, 1 },

    { ngx_string("Cache-Control"),
                 ngx_http_upstream_process_cache_control, 0,
                 ngx_http_upstream_copy_multi_header_lines,
                 offsetof(ngx_http_headers_out_t, cache_control), 1 },

    { ngx_string("Expires"),
                 ngx_http_upstream_process_expires, 0,
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, expires), 1 },

    { ngx_string("Accept-Ranges"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, accept_ranges),
                 ngx_http_upstream_copy_allow_ranges,
                 offsetof(ngx_http_headers_out_t, accept_ranges), 1 },

    { ngx_string("Connection"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_ignore_header_line, 0, 0 },

    { ngx_string("Keep-Alive"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_ignore_header_line, 0, 0 },

    { ngx_string("X-Powered-By"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Expires"),
                 ngx_http_upstream_process_accel_expires, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Redirect"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, x_accel_redirect),
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Limit-Rate"),
                 ngx_http_upstream_process_limit_rate, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Buffering"),
                 ngx_http_upstream_process_buffering, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Charset"),
                 ngx_http_upstream_process_charset, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

#if (NGX_HTTP_GZIP)
    { ngx_string("Content-Encoding"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, content_encoding),
                 ngx_http_upstream_copy_content_encoding, 0, 0 },
#endif

    { ngx_null_string, NULL, 0, NULL, 0, 0 }
};


static ngx_command_t  ngx_http_upstream_commands[] = {

    { ngx_string("upstream"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE1,
      ngx_http_upstream,
      0,
      0,
      NULL },

    { ngx_string("server"),
      NGX_HTTP_UPS_CONF|NGX_CONF_1MORE,
      ngx_http_upstream_server,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_upstream_module_ctx = {
    ngx_http_upstream_add_variables,       /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_upstream_create_main_conf,    /* create main configuration */
    ngx_http_upstream_init_main_conf,      /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_upstream_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_module_ctx,         /* module context */
    ngx_http_upstream_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_upstream_vars[] = {

    { ngx_string("upstream_addr"), NULL,
      ngx_http_upstream_addr_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("upstream_status"), NULL,
      ngx_http_upstream_status_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("upstream_response_time"), NULL,
      ngx_http_upstream_response_time_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("upstream_response_length"), NULL,
      ngx_http_upstream_response_length_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

#if (NGX_HTTP_CACHE)

    { ngx_string("upstream_cache_status"), NULL,
      ngx_http_upstream_cache_status, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

#endif

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};


static ngx_http_upstream_next_t  ngx_http_upstream_next_errors[] = {
    { 500, NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { 502, NGX_HTTP_UPSTREAM_FT_HTTP_502 },
    { 503, NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { 504, NGX_HTTP_UPSTREAM_FT_HTTP_504 },
    { 404, NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { 0, 0 }
};


ngx_conf_bitmask_t  ngx_http_upstream_cache_method_mask[] = {
   { ngx_string("GET"),  NGX_HTTP_GET},
   { ngx_string("HEAD"), NGX_HTTP_HEAD },
   { ngx_string("POST"), NGX_HTTP_POST },
   { ngx_null_string, 0 }
};


ngx_conf_bitmask_t  ngx_http_upstream_ignore_headers_masks[] = {
    { ngx_string("X-Accel-Redirect"), NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT },
    { ngx_string("X-Accel-Expires"), NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES },
    { ngx_string("Expires"), NGX_HTTP_UPSTREAM_IGN_EXPIRES },
    { ngx_string("Cache-Control"), NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL },
    { ngx_string("Set-Cookie"), NGX_HTTP_UPSTREAM_IGN_SET_COOKIE },
    { ngx_null_string, 0 }
};

//一搬会在处理过程的回调函数中调用，比如ngx_http_proxy_handler，ngx_http_fastcgi_handler等，用来申请upstream大结构体
ngx_int_t ngx_http_upstream_create(ngx_http_request_t *r)
{//创建一个上游模块需要的结构，设置到r参数的客户端请求结构上面去。
    ngx_http_upstream_t  *u;

    u = r->upstream;//拿到这个请求的upstream结构,如果其cleanup成员非空，就执行清理。为啥�
    if (u && u->cleanup) {
        r->main->count++;
        ngx_http_upstream_cleanup(r);//如果已经有了upstream结构，就复用旧的。
    }

    u = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_t));//然后申请一个新的ngx_http_upstream_t
    if (u == NULL) {
        return NGX_ERROR;
    }
    r->upstream = u;
    u->peer.log = r->connection->log;//使用这个连接的日志结构
    u->peer.log_error = NGX_ERROR_ERR;
#if (NGX_THREADS)
    u->peer.lock = &r->connection->lock;
#endif
#if (NGX_HTTP_CACHE)
    r->cache = NULL;
#endif
    return NGX_OK;
}

//下面函数一般是这么被调用的: ngx_http_read_client_request_body(r, ngx_http_upstream_init);也就是读取完客户端请求的body后调用这里。
void ngx_http_upstream_init(ngx_http_request_t *r)
{//ngx_http_read_client_request_body读取完毕客户端的数据后，就会调用这里进行初始化一个upstream
    ngx_connection_t     *c;
    c = r->connection;//得到其连接结构。
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http init upstream, client timer: %d", c->read->timer_set);

    if (c->read->timer_set) {//读完了，将读事件结构定时器删除。
        ngx_del_timer(c->read);
    }
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {//如果epoll使用边缘触发
        if (!c->write->active) {//要增加可写事件通知，为啥?因为待会可能就能写了
            if (ngx_add_event(c->write, NGX_WRITE_EVENT, NGX_CLEAR_EVENT)  == NGX_ERROR) {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
    }
    ngx_http_upstream_init_request(r);//进行实质的初始化。
}


static void
ngx_http_upstream_init_request(ngx_http_request_t *r)
{//ngx_http_upstream_init调用这里，此时客户端发送的数据都已经接收完毕了。
    ngx_str_t                      *host;
    ngx_uint_t                      i;
    ngx_resolver_ctx_t             *ctx, temp;
    ngx_http_cleanup_t             *cln;
    ngx_http_upstream_t            *u;
    ngx_http_core_loc_conf_t       *clcf;
    ngx_http_upstream_srv_conf_t   *uscf, **uscfp;
    ngx_http_upstream_main_conf_t  *umcf;

    if (r->aio) {//什么东西
        return;
    }
    u = r->upstream;//ngx_http_upstream_create里面设置的
#if (NGX_HTTP_CACHE)
    if (u->conf->cache) {
        ngx_int_t  rc;
        rc = ngx_http_upstream_cache(r, u);
        if (rc == NGX_BUSY) {
            r->write_event_handler = ngx_http_upstream_init_request;
            return;
        }
        r->write_event_handler = ngx_http_request_empty_handler;
        if (rc == NGX_DONE) {
            return;
        }
        if (rc != NGX_DECLINED) {
            ngx_http_finalize_request(r, rc);
            return;
        }
    }
#endif

    u->store = (u->conf->store || u->conf->store_lengths);
    if (!u->store && !r->post_action && !u->conf->ignore_client_abort) {//ignore_client_abort忽略客户端提前断开连接。这里指不忽略客户端提前断开。
        r->read_event_handler = ngx_http_upstream_rd_check_broken_connection;//设置回调需要检测连接是否有问题。
        r->write_event_handler = ngx_http_upstream_wr_check_broken_connection;
    }
    if (r->request_body) {//客户端发送过来的POST数据存放在此,ngx_http_read_client_request_body放的
        u->request_bufs = r->request_body->bufs;//记录客户端发送的数据，下面在create_request的时候拷贝到发送缓冲链接表里面的。
    }
	//如果是FCGI。下面组建好FCGI的各种头部，包括请求开始头，请求参数头，请求STDIN头。存放在u->request_bufs链接表里面。
	//如果是Proxy模块，ngx_http_proxy_create_request组件反向代理的头部啥的,放到u->request_bufs里面
    if (u->create_request(r) != NGX_OK) {//ngx_http_fastcgi_create_request
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    u->peer.local = u->conf->local;
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    u->output.alignment = clcf->directio_alignment;
    u->output.pool = r->pool;
    u->output.bufs.num = 1;
    u->output.bufs.size = clcf->client_body_buffer_size;
	//设置过滤模块的开始过滤函数为writer。也就是output_filter。在ngx_output_chain被调用已进行数据的过滤
    u->output.output_filter = ngx_chain_writer;
    u->output.filter_ctx = &u->writer;//参考ngx_chain_writer，里面会将输出buf一个个连接到这里。

    u->writer.pool = r->pool;
    if (r->upstream_states == NULL) {//数组upstream_states，保留upstream的状态信息。
        r->upstream_states = ngx_array_create(r->pool, 1, sizeof(ngx_http_upstream_state_t));
        if (r->upstream_states == NULL) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    } else {//如果已经有了，新加一个。
        u->state = ngx_array_push(r->upstream_states);
        if (u->state == NULL) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        ngx_memzero(u->state, sizeof(ngx_http_upstream_state_t));
    }
	//挂在清理回调函数，干嘛的暂不清楚
    cln = ngx_http_cleanup_add(r, 0);//环形链表，申请一个新的元素。
    if (cln == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    cln->handler = ngx_http_upstream_cleanup;//干嘛的
    cln->data = r;//指向所指的请求结构体。
    u->cleanup = &cln->handler;
/*然后就是这个函数最核心的处理部分，那就是根据upstream的类型来进行不同的操作，这里的upstream就是我们通过XXX_pass传递进来的值，
这里的upstream有可能下面几种情况。
1 XXX_pass中不包含变量。
2 XXX_pass传递的值包含了一个变量($开始).这种情况也就是说upstream的url是动态变化的，因此需要每次都解析一遍.
而第二种情况又分为2种，一种是在进入upstream之前，也就是 upstream模块的handler之中已经被resolve的地址(请看ngx_http_XXX_eval函数)，
一种是没有被resolve，此时就需要upstream模块来进行resolve。接下来的代码就是处理这部分的东西。*/
    if (u->resolved == NULL) {//上游的IP地址是否被解析过，ngx_http_fastcgi_handler调用ngx_http_fastcgi_eval会解析。
        uscf = u->conf->upstream;
    } else {
        if (u->resolved->sockaddr) {//如果地址已经被resolve过了，此时创建round robin peer
            if (ngx_http_upstream_create_round_robin_peer(r, u->resolved) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            ngx_http_upstream_connect(r, u);
            return;
        }
        host = &u->resolved->host;
        umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
        uscfp = umcf->upstreams.elts;//所有的上游模块
        for (i = 0; i < umcf->upstreams.nelts; i++) {
            uscf = uscfp[i];//找一个IP一样的上流模块
            if (uscf->host.len == host->len && ((uscf->port == 0 && u->resolved->no_port) || uscf->port == u->resolved->port)
                && ngx_memcmp(uscf->host.data, host->data, host->len) == 0) {
                goto found;
            }
        }

        temp.name = *host;
        ctx = ngx_resolve_start(clcf->resolver, &temp);//拷贝一下IP，port等
        if (ctx == NULL) {
            ngx_http_upstream_finalize_request(r, u,NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        if (ctx == NGX_NO_RESOLVER) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,  "no resolver defined to resolve %V", host);
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_BAD_GATEWAY);
            return;
        }

        ctx->name = *host;
        ctx->type = NGX_RESOLVE_A;
        ctx->handler = ngx_http_upstream_resolve_handler;
        ctx->data = r;
        ctx->timeout = clcf->resolver_timeout;
        u->resolved->ctx = ctx;
        if (ngx_resolve_name(ctx) != NGX_OK) {
            u->resolved->ctx = NULL;
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        return;
    }

found:
	//在ngx_http_upstream_init_main_conf的时候，会调用各个upstream的init方法，然后调用ngx_http_upstream_init_round_robin或者其他。
    if (uscf->peer.init(r, uscf) != NGX_OK) {//为ngx_http_upstream_init_round_robin设置的，为ngx_http_upstream_init_round_robin_peer
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_http_upstream_connect(r, u);
}


#if (NGX_HTTP_CACHE)

static ngx_int_t
ngx_http_upstream_cache(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_int_t          rc;
    ngx_http_cache_t  *c;

    c = r->cache;

    if (c == NULL) {

        switch (ngx_http_test_predicates(r, u->conf->cache_bypass)) {

        case NGX_ERROR:
            return NGX_ERROR;

        case NGX_DECLINED:
            u->cache_status = NGX_HTTP_CACHE_BYPASS;
            return NGX_DECLINED;

        default: /* NGX_OK */
            break;
        }

        if (!(r->method & u->conf->cache_methods)) {
            return NGX_DECLINED;
        }

        if (r->method & NGX_HTTP_HEAD) {
            u->method = ngx_http_core_get_method;
        }

        if (ngx_http_file_cache_new(r) != NGX_OK) {
            return NGX_ERROR;
        }

        if (u->create_key(r) != NGX_OK) {
            return NGX_ERROR;
        }

        /* TODO: add keys */

        ngx_http_file_cache_create_key(r);

        if (r->cache->header_start >= u->conf->buffer_size) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "cache key too large, increase upstream buffer size %uz",
                u->conf->buffer_size);

            r->cache = NULL;
            return NGX_DECLINED;
        }

        u->cacheable = 1;

        c = r->cache;

        c->min_uses = u->conf->cache_min_uses;
        c->body_start = u->conf->buffer_size;
        c->file_cache = u->conf->cache->data;

        u->cache_status = NGX_HTTP_CACHE_MISS;
    }

    rc = ngx_http_file_cache_open(r);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http upstream cache: %i", rc);

    switch (rc) {

    case NGX_HTTP_CACHE_UPDATING:

        if (u->conf->cache_use_stale & NGX_HTTP_UPSTREAM_FT_UPDATING) {
            u->cache_status = rc;
            rc = NGX_OK;

        } else {
            rc = NGX_HTTP_CACHE_STALE;
        }

        break;

    case NGX_OK:
        u->cache_status = NGX_HTTP_CACHE_HIT;
    }

    switch (rc) {

    case NGX_OK:

        rc = ngx_http_upstream_cache_send(r, u);

        if (rc != NGX_HTTP_UPSTREAM_INVALID_HEADER) {
            return rc;
        }

        break;

    case NGX_HTTP_CACHE_STALE:

        c->valid_sec = 0;
        u->buffer.start = NULL;
        u->cache_status = NGX_HTTP_CACHE_EXPIRED;

        break;

    case NGX_DECLINED:

        if ((size_t) (u->buffer.end - u->buffer.start) < u->conf->buffer_size) {
            u->buffer.start = NULL;

        } else {
            u->buffer.pos = u->buffer.start + c->header_start;
            u->buffer.last = u->buffer.pos;
        }

        break;

    case NGX_HTTP_CACHE_SCARCE:

        u->cacheable = 0;

        break;

    case NGX_AGAIN:

        return NGX_BUSY;

    case NGX_ERROR:

        return NGX_ERROR;

    default:

        /* cached NGX_HTTP_BAD_GATEWAY, NGX_HTTP_GATEWAY_TIME_OUT, etc. */

        u->cache_status = NGX_HTTP_CACHE_HIT;

        return rc;
    }

    r->cached = 0;

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_upstream_cache_send(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_int_t          rc;
    ngx_http_cache_t  *c;

    r->cached = 1;
    c = r->cache;

    if (c->header_start == c->body_start) {
        r->http_version = NGX_HTTP_VERSION_9;
        return ngx_http_cache_send(r);
    }

    /* TODO: cache stack */

    u->buffer = *c->buf;
    u->buffer.pos += c->header_start;

    ngx_memzero(&u->headers_in, sizeof(ngx_http_upstream_headers_in_t));

    if (ngx_list_init(&u->headers_in.headers, r->pool, 8,
                      sizeof(ngx_table_elt_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    rc = u->process_header(r);

    if (rc == NGX_OK) {

        if (ngx_http_upstream_process_headers(r, u) != NGX_OK) {
            return NGX_DONE;
        }

        return ngx_http_cache_send(r);
    }

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    /* rc == NGX_HTTP_UPSTREAM_INVALID_HEADER */

    /* TODO: delete file */

    return rc;
}

#endif


static void
ngx_http_upstream_resolve_handler(ngx_resolver_ctx_t *ctx)
{
    ngx_http_request_t            *r;
    ngx_http_upstream_t           *u;
    ngx_http_upstream_resolved_t  *ur;

    r = ctx->data;
    u = r->upstream;
    ur = u->resolved;
    if (ctx->state) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "%V could not be resolved (%i: %s)",
                      &ctx->name, ctx->state,ngx_resolver_strerror(ctx->state));
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_BAD_GATEWAY);
        return;
    }

    ur->naddrs = ctx->naddrs;
    ur->addrs = ctx->addrs;

#if (NGX_DEBUG)
    {
    in_addr_t   addr;
    ngx_uint_t  i;

    for (i = 0; i < ctx->naddrs; i++) {
        addr = ntohl(ur->addrs[i]);

        ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "name was resolved to %ud.%ud.%ud.%ud",
                       (addr >> 24) & 0xff, (addr >> 16) & 0xff,
                       (addr >> 8) & 0xff, addr & 0xff);
    }
    }
#endif

    if (ngx_http_upstream_create_round_robin_peer(r, ur) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_resolve_name_done(ctx);
    ur->ctx = NULL;

    ngx_http_upstream_connect(r, u);
}


static void
ngx_http_upstream_handler(ngx_event_t *ev)
{//这个是读写事件的统一回调函数，不过自己会根据读还是写调用对应的write_event_handler等
    ngx_connection_t     *c;
    ngx_http_request_t   *r;
    ngx_http_log_ctx_t   *ctx;
    ngx_http_upstream_t  *u;

    c = ev->data;
    r = c->data;

    u = r->upstream;
    c = r->connection;

    ctx = c->log->data;
    ctx->current_request = r;
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream request: \"%V?%V\"", &r->uri, &r->args);

    if (ev->write) {
        u->write_event_handler(r, u);//ngx_http_upstream_send_request_handler
    } else {
        u->read_event_handler(r, u);//ngx_http_upstream_process_header
    }
    ngx_http_run_posted_requests(c);
}


static void
ngx_http_upstream_rd_check_broken_connection(ngx_http_request_t *r)
{
    ngx_http_upstream_check_broken_connection(r, r->connection->read);
}


static void
ngx_http_upstream_wr_check_broken_connection(ngx_http_request_t *r)
{
    ngx_http_upstream_check_broken_connection(r, r->connection->write);
}


static void ngx_http_upstream_check_broken_connection(ngx_http_request_t *r, ngx_event_t *ev)
{
    int                  n;
    char                 buf[1];
    ngx_err_t            err;
    ngx_int_t            event;
    ngx_connection_t     *c;
    ngx_http_upstream_t  *u;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ev->log, 0,  "http upstream check client, write event:%d, \"%V\"", ev->write, &r->uri);
    c = r->connection;
    u = r->upstream;
    if (c->error) {//如果已经被标记为连接错误，则直接结束连接即可。
        if ((ngx_event_flags & NGX_USE_LEVEL_EVENT) && ev->active) {
            event = ev->write ? NGX_WRITE_EVENT : NGX_READ_EVENT;
            if (ngx_del_event(ev, event, 0) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
        if (!u->cacheable) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
        }
        return;
    }
#if (NGX_HAVE_KQUEUE)
    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        if (!ev->pending_eof) {
            return;
        }
        ev->eof = 1;
        c->error = 1;

        if (ev->kq_errno) {
            ev->error = 1;
        }

        if (!u->cacheable && u->peer.connection) {
            ngx_log_error(NGX_LOG_INFO, ev->log, ev->kq_errno, "kevent() reported that client closed prematurely "
                          "connection, so upstream connection is closed too");
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
            return;
        }
        ngx_log_error(NGX_LOG_INFO, ev->log, ev->kq_errno, "kevent() reported that client closed prematurely connection");
        if (u->peer.connection == NULL) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
        }
        return;
    }
#endif
    n = recv(c->fd, buf, 1, MSG_PEEK);//MSG_PEEK试探一个自己，看看是否有问题。
    err = ngx_socket_errno;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ev->log, err,  "http upstream recv(): %d", n);
    if (ev->write && (n >= 0 || err == NGX_EAGAIN)) {
        return;//没问题。
    }
    if ((ngx_event_flags & NGX_USE_LEVEL_EVENT) && ev->active) {
        event = ev->write ? NGX_WRITE_EVENT : NGX_READ_EVENT;
        if (ngx_del_event(ev, event, 0) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }
    if (n > 0) {
        return;
    }
    if (n == -1) {
        if (err == NGX_EAGAIN) {
            return;
        }
        ev->error = 1;
    } else { /* n == 0 */
        err = 0;
    }

    ev->eof = 1;
    c->error = 1;
    if (!u->cacheable && u->peer.connection) {
        ngx_log_error(NGX_LOG_INFO, ev->log, err,  "client closed prematurely connection, so upstream connection is closed too");
        ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_CLIENT_CLOSED_REQUEST);
        return;
    }
    ngx_log_error(NGX_LOG_INFO, ev->log, err, "client closed prematurely connection");
    if (u->peer.connection == NULL) {
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
    }
}


static void ngx_http_upstream_connect(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//调用socket,connect连接一个后端的peer,然后设置读写事件回调函数，进入发送数据的ngx_http_upstream_send_request里面
//这里负责连接后端服务，然后设置各个读写事件回调。最后如果连接建立成功，会调用ngx_http_upstream_send_request进行数据发送。
    ngx_int_t          rc;
    ngx_time_t        *tp;
    ngx_connection_t  *c;

    r->connection->log->action = "connecting to upstream";
    r->connection->single_connection = 0;
    if (u->state && u->state->response_sec) {
        tp = ngx_timeofday();//获取缓存的时间
        u->state->response_sec = tp->sec - u->state->response_sec;//记录时间状态。
        u->state->response_msec = tp->msec - u->state->response_msec;
    }
	//更新一下状态数据
    u->state = ngx_array_push(r->upstream_states);//增加一个上游模块的状态
    if (u->state == NULL) {
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    ngx_memzero(u->state, sizeof(ngx_http_upstream_state_t));
    tp = ngx_timeofday();
    u->state->response_sec = tp->sec;
    u->state->response_msec = tp->msec;
	//下面连接后的那模块，然后设置读写回调。
    rc = ngx_event_connect_peer(&u->peer);//获取一个peer，然后socket(),connect(),add_event之注册相关事件结构
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http upstream connect: %i", rc);
    if (rc == NGX_ERROR) {
        ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    u->state->peer = u->peer.name;
    if (rc == NGX_BUSY) {//如果这个peer被设置为忙碌状态，则尝试下一个，会递归回来的。
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "no live upstreams");
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_NOLIVE);//尝试恢复一些上游模块，然后递归调用ngx_http_upstream_connect进行连接。
        return;
    }

    if (rc == NGX_DECLINED) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);//试试其他的。
        return;
    }

    /* rc == NGX_OK || rc == NGX_AGAIN */
    c = u->peer.connection;//得到这个peer新建立的连接结构。
    c->data = r;//记住我这个连接属于哪个请求。
    c->write->handler = ngx_http_upstream_handler;//设置这个连接的读写事件结构。这是真正的读写事件回调。里面会调用write_event_handler。
    c->read->handler = ngx_http_upstream_handler;//这个是读写事件的统一回调函数，不过自己会根据读还是写调用对应的write_event_handler等

	//一个upstream的读写回调，专门做跟upstream有关的事情。上面的基本读写事件回调ngx_http_upstream_handler会调用下面的函数完成upstream对应的事情。
	u->write_event_handler = ngx_http_upstream_send_request_handler;//设置写事件的处理函数。
    u->read_event_handler = ngx_http_upstream_process_header;//读回调

    c->sendfile &= r->connection->sendfile;
    u->output.sendfile = c->sendfile;

    c->pool = r->pool;
    c->log = r->connection->log;
    c->read->log = c->log;
    c->write->log = c->log;
    /* init or reinit the ngx_output_chain() and ngx_chain_writer() contexts */
    u->writer.out = NULL;//这是i用来给ngx_chain_write函数记录发送缓冲区的。
    u->writer.last = &u->writer.out;//指向自己的头部。形成循环的结构。
    u->writer.connection = c;
    u->writer.limit = 0;

    if (u->request_sent) {//如果是已经发送了请求。却还需要连接，得重新初始化一下
        if (ngx_http_upstream_reinit(r, u) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    if (r->request_body //客户端发送过来的POST数据存放在此,ngx_http_read_client_request_body放的
		&& r->request_body->buf && r->request_body->temp_file && r == r->main)
    {//request_body是FCGI结构的数据。
        /*
         * the r->request_body->buf can be reused for one request only,
         * the subrequests should allocate their own temporay bufs
         */
        u->output.free = ngx_alloc_chain_link(r->pool);
        if (u->output.free == NULL) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }//待会需要释放的东西。
        //指向请求的BODY数据部分，啥意思?把数据放到output变量里面，待会到send_request里面会拷贝到输出链表进行发送
        u->output.free->buf = r->request_body->buf;
        u->output.free->next = NULL;
        u->output.allocated = 1;
		//清空这块内存，干嘛呢?因为请求的FCGI数据已经拷贝到了ngx_http_upstream_s的request_bufs链接表里面
        r->request_body->buf->pos = r->request_body->buf->start;
        r->request_body->buf->last = r->request_body->buf->start;
        r->request_body->buf->tag = u->output.tag;
    }

    u->request_sent = 0;//还没发送请求体呢。
    if (rc == NGX_AGAIN) {//如果刚才的rc表示连接尚未建立，则设置连接超时时间。
        ngx_add_timer(c->write, u->conf->connect_timeout);
        return;
    }
#if (NGX_HTTP_SSL)
    if (u->ssl && c->ssl == NULL) {
        ngx_http_upstream_ssl_init_connection(r, u, c);
        return;
    }
#endif
    ngx_http_upstream_send_request(r, u);//已经连接成功后端，下面进行数据发送。
}


#if (NGX_HTTP_SSL)

static void
ngx_http_upstream_ssl_init_connection(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_connection_t *c)
{
    ngx_int_t   rc;

    if (ngx_ssl_create_connection(u->conf->ssl, c,
                                  NGX_SSL_BUFFER|NGX_SSL_CLIENT)
        != NGX_OK)
    {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    c->sendfile = 0;
    u->output.sendfile = 0;

    if (u->conf->ssl_session_reuse) {
        if (u->peer.set_session(&u->peer, u->peer.data) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u,
                                               NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    r->connection->log->action = "SSL handshaking to upstream";

    rc = ngx_ssl_handshake(c);

    if (rc == NGX_AGAIN) {
        c->ssl->handler = ngx_http_upstream_ssl_handshake;
        return;
    }

    ngx_http_upstream_ssl_handshake(c);
}


static void
ngx_http_upstream_ssl_handshake(ngx_connection_t *c)
{
    ngx_http_request_t   *r;
    ngx_http_upstream_t  *u;

    r = c->data;
    u = r->upstream;

    if (c->ssl->handshaked) {

        if (u->conf->ssl_session_reuse) {
            u->peer.save_session(&u->peer, u->peer.data);
        }

        c->write->handler = ngx_http_upstream_handler;
        c->read->handler = ngx_http_upstream_handler;

        ngx_http_upstream_send_request(r, u);

        return;
    }

    ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);

}

#endif


static ngx_int_t
ngx_http_upstream_reinit(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_chain_t  *cl;

    if (u->reinit_request(r) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_memzero(&u->headers_in, sizeof(ngx_http_upstream_headers_in_t));

    if (ngx_list_init(&u->headers_in.headers, r->pool, 8,
                      sizeof(ngx_table_elt_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* reinit the request chain */

    for (cl = u->request_bufs; cl; cl = cl->next) {
        cl->buf->pos = cl->buf->start;
        cl->buf->file_pos = 0;
    }

    /* reinit the subrequest's ngx_output_chain() context */

    if (r->request_body && r->request_body->temp_file
        && r != r->main && u->output.buf)
    {
        u->output.free = ngx_alloc_chain_link(r->pool);
        if (u->output.free == NULL) {
            return NGX_ERROR;
        }

        u->output.free->buf = u->output.buf;
        u->output.free->next = NULL;

        u->output.buf->pos = u->output.buf->start;
        u->output.buf->last = u->output.buf->start;
    }

    u->output.buf = NULL;
    u->output.in = NULL;
    u->output.busy = NULL;

    /* reinit u->buffer */

    u->buffer.pos = u->buffer.start;

#if (NGX_HTTP_CACHE)

    if (r->cache) {
        u->buffer.pos += r->cache->header_start;
    }

#endif

    u->buffer.last = u->buffer.pos;

    return NGX_OK;
}


static void
ngx_http_upstream_send_request(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//调用输出的过滤器，发送数据到后端
    ngx_int_t          rc;
    ngx_connection_t  *c;
    c = u->peer.connection;//拿到这个peer的连接
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream send request");
	//测试一个连接状态，如果连接损坏，则重试
    if (!u->request_sent && ngx_http_upstream_test_connect(c) != NGX_OK) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }
    c->log->action = "sending request to upstream";
	//下面开始过滤模块的过程。对请求的FCGI数据进行过滤，里面会调用ngx_chain_writer，将数据用writev发送出去
	//ngx_http_upstream_connect将客户端发送的数据拷贝到这里，如果是从读写事件回调进入的，则这里的request_sent应该为1，
	//表示数据已经拷贝到输出链了。这份数据是在ngx_http_upstream_init_request里面调用处理模块比如FCGI的create_request处理的，解析为FCGI的结构数据。
    rc = ngx_output_chain(&u->output, u->request_sent ? NULL : u->request_bufs);
    u->request_sent = 1;//标志位数据已经发送完毕,指的是放入输出列表里面，不一定发送出去了。

    if (rc == NGX_ERROR) {//如果出错，继续
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }
    if (c->write->timer_set) {//已经不需要写数据了。
        ngx_del_timer(c->write);
    }

    if (rc == NGX_AGAIN) {//数据还没有发送完毕，待会还需要发送。
        ngx_add_timer(c->write, u->conf->send_timeout);
        if (ngx_handle_write_event(c->write, u->conf->send_lowat) != NGX_OK) {//注册一下读写事件。
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        return;
    }
    /* rc == NGX_OK */
    if (c->tcp_nopush == NGX_TCP_NOPUSH_SET) {
        if (ngx_tcp_push(c->fd) == NGX_ERROR) {//设置PUSH标志位，尽快发送数据。
            ngx_log_error(NGX_LOG_CRIT, c->log, ngx_socket_errno, ngx_tcp_push_n " failed");
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        c->tcp_nopush = NGX_TCP_NOPUSH_UNSET;
    }
    ngx_add_timer(c->read, u->conf->read_timeout);//这回数据已经发送了，可以准备接收了，设置接收超时定时器。
#if 1
    if (c->read->ready) {//如果读已经ready了，那么，你懂的，去读个头啊
        /* post aio operation */
        /* TODO comment
         * although we can post aio operation just in the end
         * of ngx_http_upstream_connect() CHECK IT !!!
         * it's better to do here because we postpone header buffer allocation
         */
        ngx_http_upstream_process_header(r, u);///处理上游发送的响应头。
        return;
    }
#endif
    u->write_event_handler = ngx_http_upstream_dummy_handler;//不用写了，只需要读
    if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
	//下面，由于这个句柄还没数据可以读取，我们可以先回去干其他事情了，因为在ngx_http_upstream_connect里面已经设置了读数据的回调函数了的。
}


static void
ngx_http_upstream_send_request_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//如果句柄可以写了，则调用这里准备发送数据到后端去。此处只处理一下写超时等。
    ngx_connection_t  *c;

    c = u->peer.connection;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http upstream send request handler");

    if (c->write->timedout) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }

#if (NGX_HTTP_SSL)
    if (u->ssl && c->ssl == NULL) {
        ngx_http_upstream_ssl_init_connection(r, u, c);
        return;
    }
#endif
    if (u->header_sent) {
        u->write_event_handler = ngx_http_upstream_dummy_handler;
        (void) ngx_handle_write_event(c->write, 0);
        return;
    }
    ngx_http_upstream_send_request(r, u);//发送数据到后端
}


static void ngx_http_upstream_process_header(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//读取FCGI头部数据，或者proxy头部数据。
    ssize_t            n;
    ngx_int_t          rc;
    ngx_connection_t  *c;
    c = u->peer.connection;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream process header");
    c->log->action = "reading response header from upstream";
    if (c->read->timedout) {//读超时了，轮询下一个。错误信息应该已经打印了
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }
	//我已发送请求，但连接出问题了
    if (!u->request_sent && ngx_http_upstream_test_connect(c) != NGX_OK) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }
    if (u->buffer.start == NULL) {//分配一块缓存，用来存放接受回来的数据。
        u->buffer.start = ngx_palloc(r->pool, u->conf->buffer_size);
        if (u->buffer.start == NULL) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        u->buffer.pos = u->buffer.start;
        u->buffer.last = u->buffer.start;
        u->buffer.end = u->buffer.start + u->conf->buffer_size;
        u->buffer.temporary = 1;
        u->buffer.tag = u->output.tag;
		//初始化headers_in存放头部信息
        if (ngx_list_init(&u->headers_in.headers, r->pool, 8, sizeof(ngx_table_elt_t))
            != NGX_OK){
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
#if (NGX_HTTP_CACHE)
        if (r->cache) {
            u->buffer.pos += r->cache->header_start;
            u->buffer.last = u->buffer.pos;
        }
#endif
    }

    for ( ;; ) {//不断调recv读取数据，如果没有了，就先返回
        n = c->recv(c, u->buffer.last, u->buffer.end - u->buffer.last);
        if (n == NGX_AGAIN) {//还没有读完，还需要关注这个事情。
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            return;
        }
        if (n == 0) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,  "upstream prematurely closed connection");
        }
        if (n == NGX_ERROR || n == 0) {//失败重试
            ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
            return;
        }
        u->buffer.last += n;//成功，移动读取到的字节数指到最后面
        rc = u->process_header(r);//ngx_http_fastcgi_process_header等，进行数据处理，比如后端返回的数据头部解析，body读取等。
        if (rc == NGX_AGAIN) {
            if (u->buffer.pos == u->buffer.end) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,"upstream sent too big header");
                ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_INVALID_HEADER);
                return;
            }
            continue;//继续。请求的HTTP头部没有处理完毕。
        }
        break;//到这里说明请求的头部已经解析完毕了。下面只剩下body了
    }
    if (rc == NGX_HTTP_UPSTREAM_INVALID_HEADER) {//头部格式错误。尝试下一个服务器。
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_INVALID_HEADER);
        return;
    }
    if (rc == NGX_ERROR) {//出错，结束请求。
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    /* rc == NGX_OK */
    if (u->headers_in.status_n > NGX_HTTP_SPECIAL_RESPONSE) {//如果状态码大于300
        if (r->subrequest_in_memory) {
            u->buffer.last = u->buffer.pos;
        }
        if (ngx_http_upstream_test_next(r, u) == NGX_OK) {
            return;
        }
        if (ngx_http_upstream_intercept_errors(r, u) == NGX_OK) {
            return;
        }
    }
    if (ngx_http_upstream_process_headers(r, u) != NGX_OK) {
        return;//解析请求的头部字段。每行HEADER回调其copy_handler，然后拷贝一下状态码等。
    }
    if (!r->subrequest_in_memory) {//如果没有子请求了，那就直接发送响应给客户端吧。
        ngx_http_upstream_send_response(r, u);
        return;
    }
	//如果还有子请求的话。子请求不是标准HTTP。
    /* subrequest content in memory */
    if (u->input_filter == NULL) {
        u->input_filter_init = ngx_http_upstream_non_buffered_filter_init;
        u->input_filter = ngx_http_upstream_non_buffered_filter;
        u->input_filter_ctx = r;
    }
    if (u->input_filter_init(u->input_filter_ctx) == NGX_ERROR) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    n = u->buffer.last - u->buffer.pos;
    if (n) {
        u->buffer.last -= n;
        u->state->response_length += n;
        if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {
            ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
            return;
        }
        if (u->length == 0) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    u->read_event_handler = ngx_http_upstream_process_body_in_memory;//设置body部分的读事件回调。
    ngx_http_upstream_process_body_in_memory(r, u);
}


static ngx_int_t
ngx_http_upstream_test_next(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_uint_t                 status;
    ngx_http_upstream_next_t  *un;

    status = u->headers_in.status_n;

    for (un = ngx_http_upstream_next_errors; un->status; un++) {

        if (status != un->status) {
            continue;
        }

        if (u->peer.tries > 1 && (u->conf->next_upstream & un->mask)) {
            ngx_http_upstream_next(r, u, un->mask);
            return NGX_OK;
        }

#if (NGX_HTTP_CACHE)

        if (u->cache_status == NGX_HTTP_CACHE_EXPIRED
            && (u->conf->cache_use_stale & un->mask))
        {
            ngx_int_t  rc;

            rc = u->reinit_request(r);

            if (rc == NGX_OK) {
                u->cache_status = NGX_HTTP_CACHE_STALE;
                rc = ngx_http_upstream_cache_send(r, u);
            }

            ngx_http_upstream_finalize_request(r, u, rc);
            return NGX_OK;
        }

#endif
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_upstream_intercept_errors(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    ngx_int_t                  status;
    ngx_uint_t                 i;
    ngx_table_elt_t           *h;
    ngx_http_err_page_t       *err_page;
    ngx_http_core_loc_conf_t  *clcf;

    status = u->headers_in.status_n;

    if (status == NGX_HTTP_NOT_FOUND && u->conf->intercept_404) {
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_NOT_FOUND);
        return NGX_OK;
    }

    if (!u->conf->intercept_errors) {
        return NGX_DECLINED;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->error_pages == NULL) {
        return NGX_DECLINED;
    }

    err_page = clcf->error_pages->elts;
    for (i = 0; i < clcf->error_pages->nelts; i++) {

        if (err_page[i].status == status) {

            if (status == NGX_HTTP_UNAUTHORIZED
                && u->headers_in.www_authenticate)
            {
                h = ngx_list_push(&r->headers_out.headers);

                if (h == NULL) {
                    ngx_http_upstream_finalize_request(r, u,
                                               NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return NGX_OK;
                }

                *h = *u->headers_in.www_authenticate;

                r->headers_out.www_authenticate = h;
            }

#if (NGX_HTTP_CACHE)

            if (r->cache) {
                time_t  valid;

                valid = ngx_http_file_cache_valid(u->conf->cache_valid, status);

                if (valid) {
                    r->cache->valid_sec = ngx_time() + valid;
                    r->cache->error = status;
                }

                ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
            }
#endif
            ngx_http_upstream_finalize_request(r, u, status);

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_upstream_test_connect(ngx_connection_t *c)
{
    int        err;
    socklen_t  len;

#if (NGX_HAVE_KQUEUE)
    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT)  {
        if (c->write->pending_eof) {
            c->log->action = "connecting to upstream";
            (void) ngx_connection_error(c, c->write->kq_errno,
                                    "kevent() reported that connect() failed");
            return NGX_ERROR;
        }
    } else
#endif
    {
        err = 0;
        len = sizeof(int);
        /*
         * BSDs and Linux return 0 and set a pending error in err
         * Solaris returns -1 and sets errno
         */
        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len)
            == -1)
        {
            err = ngx_errno;
        }

        if (err) {
            c->log->action = "connecting to upstream";
            (void) ngx_connection_error(c, err, "connect() failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_headers(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//解析请求的头部字段。每行HEADER回调其copy_handler，然后拷贝一下状态码等。拷贝头部字段到headers_out
    ngx_str_t                      *uri, args;
    ngx_uint_t                      i, flags;
    ngx_list_part_t                *part;
    ngx_table_elt_t                *h;
    ngx_http_upstream_header_t     *hh;
    ngx_http_upstream_main_conf_t  *umcf;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    if (u->headers_in.x_accel_redirect && !(u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT)) {
//如果头部中使用了X-Accel-Redirect特性，也就是下载文件的特性，则在这里进行文件下载。，重定向。
        ngx_http_upstream_finalize_request(r, u, NGX_DECLINED);
        part = &u->headers_in.headers.part;
        h = part->elts;
        for (i = 0; /* void */; i++) {
            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                h = part->elts;
                i = 0;
            }
            hh = ngx_hash_find(&umcf->headers_in_hash, h[i].hash, h[i].lowcase_key, h[i].key.len);
            if (hh && hh->redirect) {
                if (hh->copy_handler(r, &h[i], hh->conf) != NGX_OK) {
                    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return NGX_DONE;
                }
            }
        }
        uri = &u->headers_in.x_accel_redirect->value;
        ngx_str_null(&args);
        flags = NGX_HTTP_LOG_UNSAFE;
        if (ngx_http_parse_unsafe_uri(r, uri, &args, &flags) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
            return NGX_DONE;
        }
        if (r->method != NGX_HTTP_HEAD) {
            r->method = NGX_HTTP_GET;
        }
        r->valid_unparsed_uri = 0;
        ngx_http_internal_redirect(r, uri, &args);//使用内部重定向，巧妙的下载。里面又会走到各种请求处理阶段。
        ngx_http_finalize_request(r, NGX_DONE);//完毕，关闭请求。
        return NGX_DONE;
    }//X-Accel-Redirect结束

    part = &u->headers_in.headers.part;
    h = part->elts;
//处理每一个头部HEADER行，回调其挂载的句柄
    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }
        if (ngx_hash_find(&u->conf->hide_headers_hash, h[i].hash, h[i].lowcase_key, h[i].key.len)){
            continue;
        }
        hh = ngx_hash_find(&umcf->headers_in_hash, h[i].hash, h[i].lowcase_key, h[i].key.len);
        if (hh) {//回调请求头的句柄，全部注册在这个数组里面ngx_http_upstream_headers_in.
            if (hh->copy_handler(r, &h[i], hh->conf) != NGX_OK) {//一个个拷贝到请求的headers_out里面
                ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return NGX_DONE;
            }
            continue;
        }
		///如果没有注册句柄，那就老老实实的拷贝一下头部就行了、
        if (ngx_http_upstream_copy_header_line(r, &h[i], 0) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_DONE;
        }
    }

    if (r->headers_out.server && r->headers_out.server->value.data == NULL) {
        r->headers_out.server->hash = 0;
    }
    if (r->headers_out.date && r->headers_out.date->value.data == NULL) {
        r->headers_out.date->hash = 0;
    }
	//拷贝状态行，因为这个不是存在headers_in里面的。
    r->headers_out.status = u->headers_in.status_n;
    r->headers_out.status_line = u->headers_in.status_line;
    u->headers_in.content_length_n = r->headers_out.content_length_n;
    if (r->headers_out.content_length_n != -1) {
        u->length = (size_t) r->headers_out.content_length_n;

    } else {
        u->length = NGX_MAX_SIZE_T_VALUE;
    }
    return NGX_OK;
}


static void
ngx_http_upstream_process_body_in_memory(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    size_t             size;
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_event_t       *rev;
    ngx_connection_t  *c;

    c = u->peer.connection;
    rev = c->read;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream process body on memory");
    if (rev->timedout) {
        ngx_connection_error(c, NGX_ETIMEDOUT, "upstream timed out");
        ngx_http_upstream_finalize_request(r, u, NGX_ETIMEDOUT);
        return;
    }

    b = &u->buffer;//得到缓冲区
    for ( ;; ) {
        size = b->end - b->last;//缓冲区太小了。这个缓冲区是包括所有的从后端发过来的数据的、
        if (size == 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "upstream buffer is too small to read response");
            ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
            return;
        }
        n = c->recv(c, b->last, size);//还要读这么多数据，是顶多读这么多数据吧?
        if (n == NGX_AGAIN) {//如果没有读取完毕那么多
            break;
        }
        if (n == 0 || n == NGX_ERROR) {
            ngx_http_upstream_finalize_request(r, u, n);
            return;
        }
        u->state->response_length += n;//长度不断加
        if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {
            ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
            return;
        }
        if (!rev->ready) {
            break;
        }
    }

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
        return;
    }

    if (rev->active) {
        ngx_add_timer(rev, u->conf->read_timeout);

    } else if (rev->timer_set) {
        ngx_del_timer(rev);
    }
}


static void ngx_http_upstream_send_response(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//发送请求数据给客户端 http://www.pagefault.info/?p=324
    int                        tcp_nodelay;
    ssize_t                    n;
    ngx_int_t                  rc;
    ngx_event_pipe_t          *p;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    rc = ngx_http_send_header(r);//调用每一个filter过滤，处理头部数据。最后将数据发送给客户端。
    if (rc == NGX_ERROR || rc > NGX_OK || r->post_action) {
        ngx_http_upstream_finalize_request(r, u, rc);
        return;
    }
    c = r->connection;
    if (r->header_only) {
        if (u->cacheable || u->store) {
            if (ngx_shutdown_socket(c->fd, NGX_WRITE_SHUTDOWN) == -1) {
                ngx_connection_error(c, ngx_socket_errno, ngx_shutdown_socket_n " failed");
            }
            r->read_event_handler = ngx_http_request_empty_handler;
            r->write_event_handler = ngx_http_request_empty_handler;
            c->error = 1;
        } else {
            ngx_http_upstream_finalize_request(r, u, rc);
            return;
        }
    }
    u->header_sent = 1;//标记已经发送了头部字段。
    if (r->request_body && r->request_body->temp_file) {//删除客户端发送的数据局体
        ngx_pool_run_cleanup_file(r->pool, r->request_body->temp_file->file.fd);
        r->request_body->temp_file->file.fd = NGX_INVALID_FILE;
    }
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (!u->buffering) {
//这个标记的含义就是nginx是否会尽可能多的读取upstream的数据。如果关闭，则就是一个同步的发送，
//也就是接收多少，发送给客户端多少。默认这个是打开的。也就是nginx会buf住upstream发送的数据。
        if (u->input_filter == NULL) {//如果input_filter为空，则设置默认的filter
            u->input_filter_init = ngx_http_upstream_non_buffered_filter_init;//实际上为空函数
            u->input_filter = ngx_http_upstream_non_buffered_filter;
            u->input_filter_ctx = r;
        }
		//设置读写函数回调
        u->read_event_handler = ngx_http_upstream_process_non_buffered_upstream;
        r->write_event_handler = ngx_http_upstream_process_non_buffered_downstream;//调用过滤模块一个个过滤body，最终发送出去。
        r->limit_rate = 0;
        if (u->input_filter_init(u->input_filter_ctx) == NGX_ERROR) {//调用input filter 初始化函数
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
        if (clcf->tcp_nodelay && c->tcp_nodelay == NGX_TCP_NODELAY_UNSET) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "tcp_nodelay");
            tcp_nodelay = 1;//打开nodelay，准备将数据完全发送出去
            if (setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, (const void *) &tcp_nodelay, sizeof(int)) == -1) {
                ngx_connection_error(c, ngx_socket_errno,  "setsockopt(TCP_NODELAY) failed");
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            c->tcp_nodelay = NGX_TCP_NODELAY_SET;
        }
        n = u->buffer.last - u->buffer.pos;//得到将要发送的数据的大小，每次有多少就发送多少。不等待upstream了
        if (n) {
            u->buffer.last = u->buffer.pos;//注意这里，可以看到buffer被reset了。
            u->state->response_length += n;//设置将要发送的数据大小
            if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {//调用input filter
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            ngx_http_upstream_process_non_buffered_downstream(r);//最终开始发送数据到downstream
        } else {//说明buffer是空
            u->buffer.pos = u->buffer.start;
            u->buffer.last = u->buffer.start;
            if (ngx_http_send_special(r, NGX_HTTP_FLUSH) == NGX_ERROR) {//此时刷新数据到client
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            if (u->peer.connection->read->ready) {//如果可读，则继续读取upstream的数据.
                ngx_http_upstream_process_non_buffered_upstream(r, u);
            }
        }
        return;
    }
//下面就是要进行后端数据缓存处理的过程了。也就是使用了buffering标记的条件下
    /* TODO: preallocate event_pipe bufs, look "Content-Length" */
#if (NGX_HTTP_CACHE)//先不管cache
    if (r->cache && r->cache->file.fd != NGX_INVALID_FILE) {
        ngx_pool_run_cleanup_file(r->pool, r->cache->file.fd);
        r->cache->file.fd = NGX_INVALID_FILE;
    }
    switch (ngx_http_test_predicates(r, u->conf->no_cache)) {
    case NGX_ERROR:
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    case NGX_DECLINED:
        u->cacheable = 0;
        break;
    default: /* NGX_OK */
        if (u->cache_status == NGX_HTTP_CACHE_BYPASS) {

            if (ngx_http_file_cache_new(r) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            if (u->create_key(r) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            /* TODO: add keys */
            r->cache->min_uses = u->conf->cache_min_uses;
            r->cache->body_start = u->conf->buffer_size;
            r->cache->file_cache = u->conf->cache->data;
            if (ngx_http_file_cache_create(r) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            u->cacheable = 1;
        }
        break;
    }
    if (u->cacheable) {
        time_t  now, valid;
        now = ngx_time();
        valid = r->cache->valid_sec;
        if (valid == 0) {
            valid = ngx_http_file_cache_valid(u->conf->cache_valid, u->headers_in.status_n);
            if (valid) {
                r->cache->valid_sec = now + valid;
            }
        }
        if (valid) {
            r->cache->last_modified = r->headers_out.last_modified_time;
            r->cache->date = now;
            r->cache->body_start = (u_short) (u->buffer.pos - u->buffer.start);
            ngx_http_file_cache_set_header(r, u->buffer.start);
        } else {
            u->cacheable = 0;
            r->headers_out.last_modified_time = -1;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http cacheable: %d", u->cacheable);
    if (u->cacheable == 0 && r->cache) {
        ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
    }
#endif
    p = u->pipe;
    p->output_filter = (ngx_event_pipe_output_filter_pt) ngx_http_output_filter;//设置filter，可以看到就是http的输出filter
    p->output_ctx = r;
    p->tag = u->output.tag;
    p->bufs = u->conf->bufs;//设置bufs，它就是upstream中设置的bufs
    p->busy_size = u->conf->busy_buffers_size;
    p->upstream = u->peer.connection;
    p->downstream = c;
    p->pool = r->pool;
    p->log = c->log;

    p->cacheable = u->cacheable || u->store;
    p->temp_file = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
    if (p->temp_file == NULL) {
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    p->temp_file->file.fd = NGX_INVALID_FILE;
    p->temp_file->file.log = c->log;
    p->temp_file->path = u->conf->temp_path;
    p->temp_file->pool = r->pool;
    if (p->cacheable) {
        p->temp_file->persistent = 1;
    } else {
        p->temp_file->log_level = NGX_LOG_WARN;
        p->temp_file->warn = "an upstream response is buffered to a temporary file";
    }
    p->max_temp_file_size = u->conf->max_temp_file_size;
    p->temp_file_write_size = u->conf->temp_file_write_size;
    p->preread_bufs = ngx_alloc_chain_link(r->pool);
    if (p->preread_bufs == NULL) {
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    p->preread_bufs->buf = &u->buffer;
    p->preread_bufs->next = NULL;
    u->buffer.recycled = 1;
    p->preread_size = u->buffer.last - u->buffer.pos;
    if (u->cacheable) {
        p->buf_to_file = ngx_calloc_buf(r->pool);
        if (p->buf_to_file == NULL) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
        p->buf_to_file->pos = u->buffer.start;
        p->buf_to_file->last = u->buffer.pos;
        p->buf_to_file->temporary = 1;
    }
    if (ngx_event_flags & NGX_USE_AIO_EVENT) {
        /* the posted aio operation may currupt a shadow buffer */
        p->single_buf = 1;
    }
    /* TODO: p->free_bufs = 0 if use ngx_create_chain_of_bufs() */
    p->free_bufs = 1;
    /*
     * event_pipe would do u->buffer.last += p->preread_size
     * as though these bytes were read
     */
    u->buffer.last = u->buffer.pos;
    if (u->conf->cyclic_temp_file) {
        /*
         * we need to disable the use of sendfile() if we use cyclic temp file
         * because the writing a new data may interfere with sendfile()
         * that uses the same kernel file pages (at least on FreeBSD)
         */
        p->cyclic_temp_file = 1;
        c->sendfile = 0;
    } else {
        p->cyclic_temp_file = 0;
    }
    p->read_timeout = u->conf->read_timeout;
    p->send_timeout = clcf->send_timeout;
    p->send_lowat = clcf->send_lowat;
    u->read_event_handler = ngx_http_upstream_process_upstream;
    r->write_event_handler = ngx_http_upstream_process_downstream;
    ngx_http_upstream_process_upstream(r, u);
}


static void
ngx_http_upstream_process_non_buffered_downstream(ngx_http_request_t *r)
{
    ngx_event_t          *wev;
    ngx_connection_t     *c;
    ngx_http_upstream_t  *u;

    c = r->connection;
    u = r->upstream;
    wev = c->write;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http upstream process non buffered downstream");
    c->log->action = "sending to client";
    if (wev->timedout) {
        c->timedout = 1;
        ngx_connection_error(c, NGX_ETIMEDOUT, "client timed out");
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    ngx_http_upstream_process_non_buffered_request(r, 1);//要立即发送，因为是一个读写事件回调函数。
}


static void
ngx_http_upstream_process_non_buffered_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    ngx_connection_t  *c;

    c = u->peer.connection;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http upstream process non buffered upstream");
    c->log->action = "reading upstream";
    if (c->read->timedout) {
        ngx_connection_error(c, NGX_ETIMEDOUT, "upstream timed out");
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    ngx_http_upstream_process_non_buffered_request(r, 0);
}


static void
ngx_http_upstream_process_non_buffered_request(ngx_http_request_t *r, ngx_uint_t do_write)
{//调用过滤模块，将数据发送出去
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_int_t                  rc;
    ngx_connection_t          *downstream, *upstream;
    ngx_http_upstream_t       *u;
    ngx_http_core_loc_conf_t  *clcf;

    u = r->upstream;
    downstream = r->connection;//找到这个请求的客户端连接
    upstream = u->peer.connection;//找到上游的连接
    b = &u->buffer;//找到这坨要发送的数据
    do_write = do_write || u->length == 0;//do_write为1时表示要立即发送给客户端。
    for ( ;; ) {
        if (do_write) {//要立即发送。
            if (u->out_bufs || u->busy_bufs) {
				//如果u->out_bufs不为NULL则说明有需要发送的数据，如果u->busy_bufs，则说明上次有未发送完毕的数据.
                rc = ngx_http_output_filter(r, u->out_bufs);//一个个调用过滤模块，最终发送数据。
                if (rc == NGX_ERROR) {
                    ngx_http_upstream_finalize_request(r, u, 0);
                    return;
                }
                ngx_chain_update_chains(&u->free_bufs, &u->busy_bufs, &u->out_bufs, u->output.tag);//更新busy chain
            }
            if (u->busy_bufs == NULL) {//这里说明想要发送的数据都已经发送完毕
                if (u->length == 0 || upstream->read->eof || upstream->read->error) {
					//此时finalize request，结束这次请求
                    ngx_http_upstream_finalize_request(r, u, 0);
                    return;
                }
                b->pos = b->start;//否则重置u->buffer,以便与下次使用
                b->last = b->start;
            }
        }
        size = b->end - b->last;//得到当前buf的剩余空间
        if (size > u->length) {
            size = u->length;
        }
        if (size && upstream->read->ready) {//如果还有数据需要接受，并且upstream可读，则读取数据
            n = upstream->recv(upstream, b->last, size);//为什么这里还有可读数据呢，不是已经接到FCGI的结束包了吗
            if (n == NGX_AGAIN) {
                break;
            }
            if (n > 0) {//读了一些数据，将它发送出去吧，也就是放入到out_bufs链表里面去
                u->state->response_length += n;//再次调用input_filter,这里没有reset u->buffer.last,这是因为我们这个值并没有更新.
                if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {//就是ngx_http_upstream_non_buffered_filter
                    ngx_http_upstream_finalize_request(r, u, 0);
                    return;
                }
            }
            do_write = 1;//设置do_write,然后发送数据.表示还要发送数据
            continue;
        }
        break;
    }
	//下面就是各种读写事件结构
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (downstream->data == r) {
        if (ngx_handle_write_event(downstream->write, clcf->send_lowat) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    if (downstream->write->active && !downstream->write->ready) {
        ngx_add_timer(downstream->write, clcf->send_timeout);
    } else if (downstream->write->timer_set) {
        ngx_del_timer(downstream->write);
    }
    if (ngx_handle_read_event(upstream->read, 0) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    if (upstream->read->active && !upstream->read->ready) {
        ngx_add_timer(upstream->read, u->conf->read_timeout);
    } else if (upstream->read->timer_set) {
        ngx_del_timer(upstream->read);
    }
}


static ngx_int_t
ngx_http_upstream_non_buffered_filter_init(void *data)
{
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_non_buffered_filter(void *data, ssize_t bytes)
{//将u->buffer.last - u->buffer.pos之间的数据放到u->out_bufs发送缓冲去链表里面。这样可写的时候就会发送给客户端。
    ngx_http_request_t  *r = data;
    ngx_buf_t            *b;
    ngx_chain_t          *cl, **ll;
    ngx_http_upstream_t  *u;

    u = r->upstream;
    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {//遍历u->out_bufs
        ll = &cl->next;
    }
    cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);//分配一个空闲的buff
    if (cl == NULL) {
        return NGX_ERROR;
    }
    *ll = cl;//将新申请的缓存链接进来。
    cl->buf->flush = 1;
    cl->buf->memory = 1;
    b = &u->buffer;//去除将要发送的这个数据，应该是客户端的返回数据体。将其放入
    cl->buf->pos = b->last;
    b->last += bytes;//往后移动
    cl->buf->last = b->last;
    cl->buf->tag = u->output.tag;
//u->length表示将要发送的数据大小如果为NGX_MAX_SIZE_T_VALUE,则说明后端协议并没有指定需要发送的大小，此时我们只需要发送我们接收到的.
    if (u->length == NGX_MAX_SIZE_T_VALUE) {
        return NGX_OK;
    }
    u->length -= bytes;//更新将要发送的数据大小
    return NGX_OK;
}


static void
ngx_http_upstream_process_downstream(ngx_http_request_t *r)
{
    ngx_event_t          *wev;
    ngx_connection_t     *c;
    ngx_event_pipe_t     *p;
    ngx_http_upstream_t  *u;

    c = r->connection;
    u = r->upstream;
    p = u->pipe;
    wev = c->write;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http upstream process downstream");
    c->log->action = "sending to client";
    if (wev->timedout) {
        if (wev->delayed) {
            wev->timedout = 0;
            wev->delayed = 0;
            if (!wev->ready) {
                ngx_add_timer(wev, p->send_timeout);
                if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
                    ngx_http_upstream_finalize_request(r, u, 0);
                }
                return;
            }
            if (ngx_event_pipe(p, wev->write) == NGX_ABORT) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
        } else {
            p->downstream_error = 1;
            c->timedout = 1;
            ngx_connection_error(c, NGX_ETIMEDOUT, "client timed out");
        }
    } else {
        if (wev->delayed) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http downstream delayed");
            if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
            }
            return;
        }
        if (ngx_event_pipe(p, 1) == NGX_ABORT) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    ngx_http_upstream_process_request(r);
}


static void
ngx_http_upstream_process_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    ngx_connection_t  *c;
    c = u->peer.connection;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream process upstream");
    c->log->action = "reading upstream";
    if (c->read->timedout) {//如果超时了
        u->pipe->upstream_error = 1;
        ngx_connection_error(c, NGX_ETIMEDOUT, "upstream timed out");
    } else {
        if (ngx_event_pipe(u->pipe, 0) == NGX_ABORT) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    ngx_http_upstream_process_request(r);
}


static void
ngx_http_upstream_process_request(ngx_http_request_t *r)
{
    ngx_uint_t            del;
    ngx_temp_file_t      *tf;
    ngx_event_pipe_t     *p;
    ngx_http_upstream_t  *u;

    u = r->upstream;
    p = u->pipe;
    if (u->peer.connection) {
        if (u->store) {
            del = p->upstream_error;
            tf = u->pipe->temp_file;
            if (p->upstream_eof || p->upstream_done) {
                if (u->headers_in.status_n == NGX_HTTP_OK && (u->headers_in.content_length_n == -1
                        || (u->headers_in.content_length_n == tf->offset)))
                {
                    ngx_http_upstream_store(r, u);
                } else {
                    del = 1;
                }
            }
            if (del && tf->file.fd != NGX_INVALID_FILE) {
                if (ngx_delete_file(tf->file.name.data) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,ngx_delete_file_n " \"%s\" failed", u->pipe->temp_file->file.name.data);
                }
            }
        }
#if (NGX_HTTP_CACHE)
        if (u->cacheable) {
            if (p->upstream_done) {
                ngx_http_file_cache_update(r, u->pipe->temp_file);
            } else if (p->upstream_eof) {
                /* TODO: check length & update cache */
                ngx_http_file_cache_update(r, u->pipe->temp_file);
            } else if (p->upstream_error) {
                ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
            }
        }
#endif
        if (p->upstream_done || p->upstream_eof || p->upstream_error) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http upstream exit: %p", p->out);
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    if (p->downstream_error) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http upstream downstream error");
        if (!u->cacheable && !u->store && u->peer.connection) {
            ngx_http_upstream_finalize_request(r, u, 0);
        }
    }
}


static void
ngx_http_upstream_store(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    size_t                  root;
    time_t                  lm;
    ngx_str_t               path;
    ngx_temp_file_t        *tf;
    ngx_ext_rename_file_t   ext;

    tf = u->pipe->temp_file;

    if (tf->file.fd == NGX_INVALID_FILE) {

        /* create file for empty 200 response */

        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return;
        }

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = u->conf->temp_path;
        tf->pool = r->pool;
        tf->persistent = 1;

        if (ngx_create_temp_file(&tf->file, tf->path, tf->pool,
                                 tf->persistent, tf->clean, tf->access)
            != NGX_OK)
        {
            return;
        }

        u->pipe->temp_file = tf;
    }

    ext.access = u->conf->store_access;
    ext.path_access = u->conf->store_access;
    ext.time = -1;
    ext.create_path = 1;
    ext.delete_file = 1;
    ext.log = r->connection->log;

    if (u->headers_in.last_modified) {

        lm = ngx_http_parse_time(u->headers_in.last_modified->value.data,
                                 u->headers_in.last_modified->value.len);

        if (lm != NGX_ERROR) {
            ext.time = lm;
            ext.fd = tf->file.fd;
        }
    }

    if (u->conf->store_lengths == NULL) {

        ngx_http_map_uri_to_path(r, &path, &root, 0);

    } else {
        if (ngx_http_script_run(r, &path, u->conf->store_lengths->elts, 0,
                                u->conf->store_values->elts)
            == NULL)
        {
            return;
        }
    }

    path.len--;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "upstream stores \"%s\" to \"%s\"",
                   tf->file.name.data, path.data);

    (void) ngx_ext_rename_file(&tf->file.name, &path, &ext);
}


static void
ngx_http_upstream_dummy_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http upstream dummy handler");
}


static void
ngx_http_upstream_next(ngx_http_request_t *r, ngx_http_upstream_t *u,
    ngx_uint_t ft_type)
{
    ngx_uint_t  status, state;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http next upstream, %xi", ft_type);

#if 0
    ngx_http_busy_unlock(u->conf->busy_lock, &u->busy_lock);
#endif

    if (ft_type == NGX_HTTP_UPSTREAM_FT_HTTP_404) {
        state = NGX_PEER_NEXT;
    } else {
        state = NGX_PEER_FAILED;
    }

    if (ft_type != NGX_HTTP_UPSTREAM_FT_NOLIVE) {
        u->peer.free(&u->peer, u->peer.data, state);
    }

    if (ft_type == NGX_HTTP_UPSTREAM_FT_TIMEOUT) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, NGX_ETIMEDOUT,
                      "upstream timed out");
    }

    if (u->peer.cached && ft_type == NGX_HTTP_UPSTREAM_FT_ERROR) {
        status = 0;

    } else {
        switch(ft_type) {

        case NGX_HTTP_UPSTREAM_FT_TIMEOUT:
            status = NGX_HTTP_GATEWAY_TIME_OUT;
            break;

        case NGX_HTTP_UPSTREAM_FT_HTTP_500:
            status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            break;

        case NGX_HTTP_UPSTREAM_FT_HTTP_404:
            status = NGX_HTTP_NOT_FOUND;
            break;

        /*
         * NGX_HTTP_UPSTREAM_FT_BUSY_LOCK and NGX_HTTP_UPSTREAM_FT_MAX_WAITING
         * never reach here
         */

        default:
            status = NGX_HTTP_BAD_GATEWAY;
        }
    }

    if (r->connection->error) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_CLIENT_CLOSED_REQUEST);
        return;
    }

    if (status) {
        u->state->status = status;

        if (u->peer.tries == 0 || !(u->conf->next_upstream & ft_type)) {

#if (NGX_HTTP_CACHE)

            if (u->cache_status == NGX_HTTP_CACHE_EXPIRED
                && (u->conf->cache_use_stale & ft_type))
            {
                ngx_int_t  rc;

                rc = u->reinit_request(r);

                if (rc == NGX_OK) {
                    u->cache_status = NGX_HTTP_CACHE_STALE;
                    rc = ngx_http_upstream_cache_send(r, u);
                }

                ngx_http_upstream_finalize_request(r, u, rc);
                return;
            }
#endif

            ngx_http_upstream_finalize_request(r, u, status);
            return;
        }
    }

    if (u->peer.connection) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "close http upstream connection: %d",
                       u->peer.connection->fd);
#if (NGX_HTTP_SSL)

        if (u->peer.connection->ssl) {
            u->peer.connection->ssl->no_wait_shutdown = 1;
            u->peer.connection->ssl->no_send_shutdown = 1;

            (void) ngx_ssl_shutdown(u->peer.connection);
        }
#endif

        ngx_close_connection(u->peer.connection);
    }

#if 0
    if (u->conf->busy_lock && !u->busy_locked) {
        ngx_http_upstream_busy_lock(p);
        return;
    }
#endif

    ngx_http_upstream_connect(r, u);
}


static void
ngx_http_upstream_cleanup(void *data)
{
    ngx_http_request_t *r = data;

    ngx_http_upstream_t  *u;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "cleanup http upstream request: \"%V\"", &r->uri);

    u = r->upstream;

    if (u->resolved && u->resolved->ctx) {
        ngx_resolve_name_done(u->resolved->ctx);
        u->resolved->ctx = NULL;
    }

    ngx_http_upstream_finalize_request(r, u, NGX_DONE);
}


static void
ngx_http_upstream_finalize_request(ngx_http_request_t *r, ngx_http_upstream_t *u, ngx_int_t rc)
{
    ngx_time_t  *tp;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "finalize http upstream request: %i", rc);
    if (u->cleanup) {
        *u->cleanup = NULL;
        u->cleanup = NULL;
    }

    if (u->resolved && u->resolved->ctx) {
        ngx_resolve_name_done(u->resolved->ctx);
        u->resolved->ctx = NULL;
    }

    if (u->state && u->state->response_sec) {
        tp = ngx_timeofday();
        u->state->response_sec = tp->sec - u->state->response_sec;
        u->state->response_msec = tp->msec - u->state->response_msec;

        if (u->pipe) {
            u->state->response_length = u->pipe->read_length;
        }
    }

    u->finalize_request(r, rc);

    if (u->peer.free) {
        u->peer.free(&u->peer, u->peer.data, 0);
    }

    if (u->peer.connection) {

#if (NGX_HTTP_SSL)

        /* TODO: do not shutdown persistent connection */

        if (u->peer.connection->ssl) {

            /*
             * We send the "close notify" shutdown alert to the upstream only
             * and do not wait its "close notify" shutdown alert.
             * It is acceptable according to the TLS standard.
             */

            u->peer.connection->ssl->no_wait_shutdown = 1;

            (void) ngx_ssl_shutdown(u->peer.connection);
        }
#endif

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "close http upstream connection: %d",
                       u->peer.connection->fd);

        ngx_close_connection(u->peer.connection);
    }

    u->peer.connection = NULL;

    if (u->pipe && u->pipe->temp_file) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http upstream temp fd: %d",
                       u->pipe->temp_file->file.fd);
    }

#if (NGX_HTTP_CACHE)

    if (u->cacheable && r->cache) {
        time_t  valid;

        if (rc == NGX_HTTP_BAD_GATEWAY || rc == NGX_HTTP_GATEWAY_TIME_OUT) {

            valid = ngx_http_file_cache_valid(u->conf->cache_valid, rc);

            if (valid) {
                r->cache->valid_sec = ngx_time() + valid;
                r->cache->error = rc;
            }
        }

        ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
    }

#endif

    if (u->header_sent
        && (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE))
    {
        rc = 0;
    }

    if (rc == NGX_DECLINED) {
        return;
    }

    r->connection->log->action = "sending to client";

    if (rc == 0) {
        rc = ngx_http_send_special(r, NGX_HTTP_LAST);
    }
    ngx_http_finalize_request(r, rc);
}


static ngx_int_t
ngx_http_upstream_process_header_line(ngx_http_request_t *r, ngx_table_elt_t *h, ngx_uint_t offset)
{//就拷贝了一下头部数据
    ngx_table_elt_t  **ph;
    ph = (ngx_table_elt_t **) ((char *) &r->upstream->headers_in + offset);
    if (*ph == NULL) {
        *ph = h;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_ignore_header_line(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_set_cookie(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
#if (NGX_HTTP_CACHE)
    ngx_http_upstream_t  *u;

    u = r->upstream;

    if (!(u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_SET_COOKIE)) {
        u->cacheable = 0;
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_cache_control(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_array_t          *pa;
    ngx_table_elt_t     **ph;
    ngx_http_upstream_t  *u;

    u = r->upstream;
    pa = &u->headers_in.cache_control;

    if (pa->elts == NULL) {
       if (ngx_array_init(pa, r->pool, 2, sizeof(ngx_table_elt_t *)) != NGX_OK)
       {
           return NGX_ERROR;
       }
    }

    ph = ngx_array_push(pa);
    if (ph == NULL) {
        return NGX_ERROR;
    }

    *ph = h;

#if (NGX_HTTP_CACHE)
    {
    u_char     *p, *last;
    ngx_int_t   n;

    if (u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL) {
        return NGX_OK;
    }

    if (r->cache == NULL) {
        return NGX_OK;
    }

    if (r->cache->valid_sec != 0) {
        return NGX_OK;
    }

    p = h->value.data;
    last = p + h->value.len;

    if (ngx_strlcasestrn(p, last, (u_char *) "no-cache", 8 - 1) != NULL
        || ngx_strlcasestrn(p, last, (u_char *) "no-store", 8 - 1) != NULL
        || ngx_strlcasestrn(p, last, (u_char *) "private", 7 - 1) != NULL)
    {
        u->cacheable = 0;
        return NGX_OK;
    }

    p = ngx_strlcasestrn(p, last, (u_char *) "max-age=", 8 - 1);

    if (p == NULL) {
        return NGX_OK;
    }

    n = 0;

    for (p += 8; p < last; p++) {
        if (*p == ',' || *p == ';' || *p == ' ') {
            break;
        }

        if (*p >= '0' && *p <= '9') {
            n = n * 10 + *p - '0';
            continue;
        }

        u->cacheable = 0;
        return NGX_OK;
    }

    if (n == 0) {
        u->cacheable = 0;
        return NGX_OK;
    }

    r->cache->valid_sec = ngx_time() + n;
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_expires(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_http_upstream_t  *u;

    u = r->upstream;
    u->headers_in.expires = h;

#if (NGX_HTTP_CACHE)
    {
    time_t  expires;

    if (u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_EXPIRES) {
        return NGX_OK;
    }

    if (r->cache == NULL) {
        return NGX_OK;
    }

    if (r->cache->valid_sec != 0) {
        return NGX_OK;
    }

    expires = ngx_http_parse_time(h->value.data, h->value.len);

    if (expires == NGX_ERROR || expires < ngx_time()) {
        u->cacheable = 0;
        return NGX_OK;
    }

    r->cache->valid_sec = expires;
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_accel_expires(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_http_upstream_t  *u;

    u = r->upstream;
    u->headers_in.x_accel_expires = h;

#if (NGX_HTTP_CACHE)
    {
    u_char     *p;
    size_t      len;
    ngx_int_t   n;

    if (u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES) {
        return NGX_OK;
    }

    if (r->cache == NULL) {
        return NGX_OK;
    }

    len = h->value.len;
    p = h->value.data;

    if (p[0] != '@') {
        n = ngx_atoi(p, len);

        switch (n) {
        case 0:
            u->cacheable = 0;
        case NGX_ERROR:
            return NGX_OK;

        default:
            r->cache->valid_sec = ngx_time() + n;
            return NGX_OK;
        }
    }

    p++;
    len--;

    n = ngx_atoi(p, len);

    if (n != NGX_ERROR) {
        r->cache->valid_sec = n;
    }
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_limit_rate(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_int_t  n;

    r->upstream->headers_in.x_accel_limit_rate = h;

    n = ngx_atoi(h->value.data, h->value.len);

    if (n != NGX_ERROR) {
        r->limit_rate = (size_t) n;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_buffering(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char  c0, c1, c2;

    if (r->upstream->conf->change_buffering) {

        if (h->value.len == 2) {
            c0 = ngx_tolower(h->value.data[0]);
            c1 = ngx_tolower(h->value.data[1]);

            if (c0 == 'n' && c1 == 'o') {
                r->upstream->buffering = 0;
            }

        } else if (h->value.len == 3) {
            c0 = ngx_tolower(h->value.data[0]);
            c1 = ngx_tolower(h->value.data[1]);
            c2 = ngx_tolower(h->value.data[2]);

            if (c0 == 'y' && c1 == 'e' && c2 == 's') {
                r->upstream->buffering = 1;
            }
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_charset(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    r->headers_out.override_charset = &h->value;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_header_line(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  *ho, **ph;
    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }
    *ho = *h;//指向新数据的位置。
    if (offset) {
        ph = (ngx_table_elt_t **) ((char *) &r->headers_out + offset);
        *ph = ho;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_multi_header_lines(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_array_t      *pa;
    ngx_table_elt_t  *ho, **ph;

    pa = (ngx_array_t *) ((char *) &r->headers_out + offset);

    if (pa->elts == NULL) {
        if (ngx_array_init(pa, r->pool, 2, sizeof(ngx_table_elt_t *)) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    ph = ngx_array_push(pa);
    if (ph == NULL) {
        return NGX_ERROR;
    }

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;
    *ph = ho;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_content_type(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char  *p, *last;

    r->headers_out.content_type_len = h->value.len;
    r->headers_out.content_type = h->value;
    r->headers_out.content_type_lowcase = NULL;

    for (p = h->value.data; *p; p++) {

        if (*p != ';') {
            continue;
        }

        last = p;

        while (*++p == ' ') { /* void */ }

        if (*p == '\0') {
            return NGX_OK;
        }

        if (ngx_strncasecmp(p, (u_char *) "charset=", 8) != 0) {
            continue;
        }

        p += 8;

        r->headers_out.content_type_len = last - h->value.data;

        if (*p == '"') {
            p++;
        }

        last = h->value.data + h->value.len;

        if (*(last - 1) == '"') {
            last--;
        }

        r->headers_out.charset.len = last - p;
        r->headers_out.charset.data = p;

        return NGX_OK;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_content_length(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    r->headers_out.content_length = ho;
    r->headers_out.content_length_n = ngx_atoof(h->value.data, h->value.len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_last_modified(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    r->headers_out.last_modified = ho;

#if (NGX_HTTP_CACHE)

    if (r->upstream->cacheable) {
        r->headers_out.last_modified_time = ngx_http_parse_time(h->value.data,
                                                                h->value.len);
    }

#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_rewrite_location(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_int_t         rc;
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    if (r->upstream->rewrite_redirect) {
        rc = r->upstream->rewrite_redirect(r, ho, 0);

        if (rc == NGX_DECLINED) {
            return NGX_OK;
        }

        if (rc == NGX_OK) {
            r->headers_out.location = ho;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "rewritten location: \"%V\"", &ho->value);
        }

        return rc;
    }

    if (ho->value.data[0] != '/') {
        r->headers_out.location = ho;
    }

    /*
     * we do not set r->headers_out.location here to avoid the handling
     * the local redirects without a host name by ngx_http_header_filter()
     */

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_rewrite_refresh(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char           *p;
    ngx_int_t         rc;
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    if (r->upstream->rewrite_redirect) {

        p = ngx_strcasestrn(ho->value.data, "url=", 4 - 1);

        if (p) {
            rc = r->upstream->rewrite_redirect(r, ho, p + 4 - ho->value.data);

        } else {
            return NGX_OK;
        }

        if (rc == NGX_DECLINED) {
            return NGX_OK;
        }

        if (rc == NGX_OK) {
            r->headers_out.refresh = ho;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "rewritten refresh: \"%V\"", &ho->value);
        }

        return rc;
    }

    r->headers_out.refresh = ho;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_allow_ranges(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

#if (NGX_HTTP_CACHE)

    if (r->cached) {
        r->allow_ranges = 1;
        return NGX_OK;

    }

#endif

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }
    *ho = *h;
    r->headers_out.accept_ranges = ho;

    return NGX_OK;
}


#if (NGX_HTTP_GZIP)

static ngx_int_t
ngx_http_upstream_copy_content_encoding(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    r->headers_out.content_encoding = ho;

    return NGX_OK;
}

#endif


static ngx_int_t
ngx_http_upstream_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_upstream_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_addr_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = 0;
    state = r->upstream_states->elts;

    for (i = 0; i < r->upstream_states->nelts; i++) {
        if (state[i].peer) {
            len += state[i].peer->len + 2;

        } else {
            len += 3;
        }
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;

    for ( ;; ) {
        if (state[i].peer) {
            p = ngx_cpymem(p, state[i].peer->data, state[i].peer->len);
        }

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_status_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = r->upstream_states->nelts * (3 + 2);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;
    state = r->upstream_states->elts;

    for ( ;; ) {
        if (state[i].status) {
            p = ngx_sprintf(p, "%ui", state[i].status);

        } else {
            *p++ = '-';
        }

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_response_time_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_msec_int_t              ms;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = r->upstream_states->nelts * (NGX_TIME_T_LEN + 4 + 2);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;
    state = r->upstream_states->elts;

    for ( ;; ) {
        if (state[i].status) {
            ms = (ngx_msec_int_t)
                     (state[i].response_sec * 1000 + state[i].response_msec);
            ms = ngx_max(ms, 0);
            p = ngx_sprintf(p, "%d.%03d", ms / 1000, ms % 1000);

        } else {
            *p++ = '-';
        }

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_response_length_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = r->upstream_states->nelts * (NGX_OFF_T_LEN + 2);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;
    state = r->upstream_states->elts;

    for ( ;; ) {
        p = ngx_sprintf(p, "%O", state[i].response_length);

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


ngx_int_t ngx_http_upstream_header_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{//获取非著名的头部字段。
    if (r->upstream == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }
    return ngx_http_variable_unknown_header(v, (ngx_str_t *) data, &r->upstream->headers_in.headers.part, sizeof("upstream_http_") - 1);
}


#if (NGX_HTTP_CACHE)

ngx_int_t
ngx_http_upstream_cache_status(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_uint_t  n;

    if (r->upstream == NULL || r->upstream->cache_status == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    n = r->upstream->cache_status - 1;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->len = ngx_http_cache_status[n].len;
    v->data = ngx_http_cache_status[n].data;

    return NGX_OK;
}

#endif


static char *
ngx_http_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{//当碰到upstream{}指令的时候调用这里。
    char                          *rv;
    void                          *mconf;
    ngx_str_t                     *value;
    ngx_url_t                      u;
    ngx_uint_t                     m;
    ngx_conf_t                     pcf;
    ngx_http_module_t             *module;
    ngx_http_conf_ctx_t           *ctx, *http_ctx;
    ngx_http_upstream_srv_conf_t  *uscf;

    ngx_memzero(&u, sizeof(ngx_url_t));
    value = cf->args->elts;
    u.host = value[1];
    u.no_resolve = 1;
	//下面将u代表的数据设置到umcf->upstreams里面去。然后返回对应的upstream{}结构数据指针。
    uscf = ngx_http_upstream_add(cf, &u, NGX_HTTP_UPSTREAM_CREATE
                                         |NGX_HTTP_UPSTREAM_WEIGHT
                                         |NGX_HTTP_UPSTREAM_MAX_FAILS
                                         |NGX_HTTP_UPSTREAM_FAIL_TIMEOUT
                                         |NGX_HTTP_UPSTREAM_DOWN
                                         |NGX_HTTP_UPSTREAM_BACKUP);
    if (uscf == NULL) {
        return NGX_CONF_ERROR;
    }
	//申请ngx_http_conf_ctx_t结构，经典的main/srv/local_conf指针结构
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    http_ctx = cf->ctx;//跟上层的HTTP公用main_conf，这里跟server{}指令一样的，共享main_conf
    ctx->main_conf = http_ctx->main_conf;

    /* the upstream{}'s srv_conf */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }
	//在ngx_http_upstream_module模块里面记录我的srv_conf。里面记录了我里面有哪几个server指令
    ctx->srv_conf[ngx_http_upstream_module.ctx_index] = uscf;//uscf里面记录了server列表信息。
    uscf->srv_conf = ctx->srv_conf;//这一条，记住我这个upstream所属的srv_conf数组。也就是所属的http{}块里面的srv_conf

    /* the upstream{}'s loc_conf */
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    for (m = 0; ngx_modules[m]; m++) {//老规矩，初始化所有HTTP模块的srv,loc配置。调用每个模块的create回调
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;
        if (module->create_srv_conf) {
            mconf = module->create_srv_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->srv_conf[ngx_modules[m]->ctx_index] = mconf;
        }
        if (module->create_loc_conf) {
            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->loc_conf[ngx_modules[m]->ctx_index] = mconf;
        }
    }

    /* parse inside upstream{} */
    pcf = *cf;
    cf->ctx = ctx;//临时切换ctx，进入upstream{}块中进行解析。
    cf->cmd_type = NGX_HTTP_UPS_CONF;
    rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }
    if (uscf->servers == NULL) { "no servers are inside upstream");
        return NGX_CONF_ERROR;
    }
    return rv;
}


static char *
ngx_http_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//解析到"server"的时候调用这里.里面只是在uscf->servers里面增加了一个server,并设置好ngx_http_upstream_server_t结构的数据。其他没干。
    ngx_http_upstream_srv_conf_t  *uscf = conf;//从ngx_conf_handler里面可以看出，这个conf就是upstream的ctx->srv_conf[upstream模块.index]的值
	//upstream模块的conf为NGX_HTTP_SRV_CONF_OFFSET，所以决定了ngx_conf_handler里面的conf参数
    time_t                       fail_timeout;
    ngx_str_t                   *value, s;
    ngx_url_t                    u;
    ngx_int_t                    weight, max_fails;
    ngx_uint_t                   i;
    ngx_http_upstream_server_t  *us;

    if (uscf->servers == NULL) {//如果本upstream的servers数组为空，初始化之
        uscf->servers = ngx_array_create(cf->pool, 4, sizeof(ngx_http_upstream_server_t));
        if (uscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }
    }
    us = ngx_array_push(uscf->servers);//增加一个server.下面如果配置失败，会直接返回失败的，也就不需要把这项删了。
    if (us == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memzero(us, sizeof(ngx_http_upstream_server_t));
    value = cf->args->elts;
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = value[1];
    u.default_port = 80;

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {//解析一下URL的数据结构。
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%s in upstream \"%V\"", u.err, &u.url);
        }
        return NGX_CONF_ERROR;
    }
    weight = 1;
    max_fails = 1;
    fail_timeout = 10;
    for (i = 2; i < cf->args->nelts; i++) {//遍历后面的每一个参数，比如: server 127.0.0.1:8080 max_fails=3 fail_timeout=30s;
        if (ngx_strncmp(value[i].data, "weight=", 7) == 0) {//得到整数类型的权重。
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_WEIGHT)) {
                goto invalid;
            }//weight = NUMBER - set weight of the server, if not set weight is equal to one.
            weight = ngx_atoi(&value[i].data[7], value[i].len - 7);
            if (weight == NGX_ERROR || weight == 0) {
                goto invalid;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "max_fails=", 10) == 0) {//解析max_fails参数，表示
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_MAX_FAILS)) {
                goto invalid;
            }//NUMBER - number of unsuccessful attempts at communicating with the server within the time period
            max_fails = ngx_atoi(&value[i].data[10], value[i].len - 10);
            if (max_fails == NGX_ERROR) {
                goto invalid;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "fail_timeout=", 13) == 0) {//这么fail_timeout多的时间内，出现max_fails的失败的服务器将被标记为出问题的。
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_FAIL_TIMEOUT)) {
                goto invalid;
            }//fail_timeout = TIME - the time during which must occur *max_fails* number of unsuccessful attempts at communication with the server that would cause the server to be considered inoperative
            s.len = value[i].len - 13;
            s.data = &value[i].data[13];
            fail_timeout = ngx_parse_time(&s, 1);
            if (fail_timeout == NGX_ERROR) {
                goto invalid;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "backup", 6) == 0) {
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_BACKUP)) {
                goto invalid;
            }//backup - (0.6.7 or later) only uses this server if the non-backup servers are all down or busy
            us->backup = 1;
            continue;
        }
        if (ngx_strncmp(value[i].data, "down", 4) == 0) {
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_DOWN)) {
                goto invalid;
            }
            us->down = 1;
            continue;
        }

        goto invalid;
    }
	//下面拷贝设置一下这个server的相关信息。
    us->addrs = u.addrs;
    us->naddrs = u.naddrs;
    us->weight = weight;
    us->max_fails = max_fails;
    us->fail_timeout = fail_timeout;
    return NGX_CONF_OK;

invalid:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid parameter \"%V\"", &value[i]);
    return NGX_CONF_ERROR;
}


ngx_http_upstream_srv_conf_t *
ngx_http_upstream_add(ngx_conf_t *cf, ngx_url_t *u, ngx_uint_t flags)
{//如果u代表的server已经存在，则返回句柄，否则在umcf->upstreams里面新加一个，设置初始化。
//在ngx_http_fastcgi_pass等碰到后端地址的地方，会调用这个函数，增加一个upstream的server.
//这里的单位是upstream，不是server行，调用的上层一般只有一个地址，于是就只有一个server.当做单个upstream处理
    ngx_uint_t                      i;
    ngx_http_upstream_server_t     *us;
    ngx_http_upstream_srv_conf_t   *uscf, **uscfp;
    ngx_http_upstream_main_conf_t  *umcf;

    if (!(flags & NGX_HTTP_UPSTREAM_CREATE)) {//如果没有设置CREATE标志，表示不需要创建。
        if (ngx_parse_url(cf->pool, u) != NGX_OK) {//简析一下地址格式，unix:域，inet6,4地址，http://host:port/等
            if (u->err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "%s in upstream \"%V\"", u->err, &u->url);
            }
            return NULL;
        }
    }

    umcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_upstream_module);
    uscfp = umcf->upstreams.elts;
    for (i = 0; i < umcf->upstreams.nelts; i++) {
		//遍历当前的upstream，如果有重复的，则比较其相关的字段，并打印日志。如果找到相同的，则返回对应指针。
        if (uscfp[i]->host.len != u->host.len || ngx_strncasecmp(uscfp[i]->host.data, u->host.data, u->host.len)  != 0) {
            continue;//不相同的不管。
        }

        if ((flags & NGX_HTTP_UPSTREAM_CREATE) && (uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE)) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "duplicate upstream \"%V\"", &u->host);
            return NULL;
        }
        if ((uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE) && u->port) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,  "upstream \"%V\" may not have port %d", &u->host, u->port);
            return NULL;
        }
        if ((flags & NGX_HTTP_UPSTREAM_CREATE) && uscfp[i]->port) {
            ngx_log_error(NGX_LOG_WARN, cf->log, 0, "upstream \"%V\" may not have port %d in %s:%ui",
                          &u->host, uscfp[i]->port, uscfp[i]->file_name, uscfp[i]->line);
            return NULL;
        }
        if (uscfp[i]->port != u->port) {
            continue;
        }
        if (uscfp[i]->default_port && u->default_port  && uscfp[i]->default_port != u->default_port)  {
            continue;
        }
        return uscfp[i];//找到相同的配置数据了，直接返回它的指针。
    }
	//没有找到相同的配置upstream，下面创建一个。这里的srv_conf跟server{}不是一回事,是指upstream{}里面的server xxxx:xxx;行
    uscf = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_srv_conf_t));
    if (uscf == NULL) {
        return NULL;
    }
    uscf->flags = flags;
    uscf->host = u->host;
    uscf->file_name = cf->conf_file->file.name.data;
    uscf->line = cf->conf_file->line;
    uscf->port = u->port;
    uscf->default_port = u->default_port;

    if (u->naddrs == 1) {//比如: server xx.xx.xx.xx:xx weight=2 max_fails=3;  刚开始，ngx_http_upstream会调用本函数。但是其naddres=0.
        uscf->servers = ngx_array_create(cf->pool, 1,  sizeof(ngx_http_upstream_server_t));
        if (uscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }
        us = ngx_array_push(uscf->servers);//记录本upstream{}块的所有server指令。
        if (us == NULL) {
            return NGX_CONF_ERROR;
        }
        ngx_memzero(us, sizeof(ngx_http_upstream_server_t));
        us->addrs = u->addrs;//拷贝地址 信息。
        us->naddrs = u->naddrs;
    }
	//
    uscfp = ngx_array_push(&umcf->upstreams);//放到upstream的main_conf里面去。
    if (uscfp == NULL) {
        return NULL;
    }
    *uscfp = uscf;//在当前的umcf->upstreams updstream配置里面增加一项。

    return uscf;
}


char *
ngx_http_upstream_bind_set_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    char  *p = conf;

    ngx_int_t     rc;
    ngx_str_t    *value;
    ngx_addr_t  **paddr;

    paddr = (ngx_addr_t **) (p + cmd->offset);

    *paddr = ngx_palloc(cf->pool, sizeof(ngx_addr_t));
    if (*paddr == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    rc = ngx_parse_addr(cf->pool, *paddr, value[1].data, value[1].len);

    switch (rc) {
    case NGX_OK:
        (*paddr)->name = value[1];
        return NGX_CONF_OK;

    case NGX_DECLINED:
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid address \"%V\"", &value[1]);
    default:
        return NGX_CONF_ERROR;
    }
}


ngx_int_t
ngx_http_upstream_hide_headers_hash(ngx_conf_t *cf,
    ngx_http_upstream_conf_t *conf, ngx_http_upstream_conf_t *prev,
    ngx_str_t *default_hide_headers, ngx_hash_init_t *hash)
{
    ngx_str_t       *h;
    ngx_uint_t       i, j;
    ngx_array_t      hide_headers;
    ngx_hash_key_t  *hk;

    if (conf->hide_headers == NGX_CONF_UNSET_PTR
        && conf->pass_headers == NGX_CONF_UNSET_PTR)
    {
        conf->hide_headers_hash = prev->hide_headers_hash;

        if (conf->hide_headers_hash.buckets
#if (NGX_HTTP_CACHE)
            && ((conf->cache == NULL) == (prev->cache == NULL))
#endif
           )
        {
            return NGX_OK;
        }

        conf->hide_headers = prev->hide_headers;
        conf->pass_headers = prev->pass_headers;

    } else {
        if (conf->hide_headers == NGX_CONF_UNSET_PTR) {
            conf->hide_headers = prev->hide_headers;
        }

        if (conf->pass_headers == NGX_CONF_UNSET_PTR) {
            conf->pass_headers = prev->pass_headers;
        }
    }

    if (ngx_array_init(&hide_headers, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (h = default_hide_headers; h->len; h++) {
        hk = ngx_array_push(&hide_headers);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = *h;
        hk->key_hash = ngx_hash_key_lc(h->data, h->len);
        hk->value = (void *) 1;
    }

    if (conf->hide_headers != NGX_CONF_UNSET_PTR) {

        h = conf->hide_headers->elts;

        for (i = 0; i < conf->hide_headers->nelts; i++) {

            hk = hide_headers.elts;

            for (j = 0; j < hide_headers.nelts; j++) {
                if (ngx_strcasecmp(h[i].data, hk[j].key.data) == 0) {
                    goto exist;
                }
            }

            hk = ngx_array_push(&hide_headers);
            if (hk == NULL) {
                return NGX_ERROR;
            }

            hk->key = h[i];
            hk->key_hash = ngx_hash_key_lc(h[i].data, h[i].len);
            hk->value = (void *) 1;

        exist:

            continue;
        }
    }

    if (conf->pass_headers != NGX_CONF_UNSET_PTR) {

        h = conf->pass_headers->elts;
        hk = hide_headers.elts;

        for (i = 0; i < conf->pass_headers->nelts; i++) {
            for (j = 0; j < hide_headers.nelts; j++) {

                if (hk[j].key.data == NULL) {
                    continue;
                }

                if (ngx_strcasecmp(h[i].data, hk[j].key.data) == 0) {
                    hk[j].key.data = NULL;
                    break;
                }
            }
        }
    }

    hash->hash = &conf->hide_headers_hash;
    hash->key = ngx_hash_key_lc;
    hash->pool = cf->pool;
    hash->temp_pool = NULL;

    return ngx_hash_init(hash, hide_headers.elts, hide_headers.nelts);
}


static void *
ngx_http_upstream_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_upstream_main_conf_t  *umcf;
    umcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_main_conf_t));
    if (umcf == NULL) {
        return NULL;
    }
    if (ngx_array_init(&umcf->upstreams, cf->pool, 4,  sizeof(ngx_http_upstream_srv_conf_t *)) != NGX_OK)  {
        return NULL;
    }
    return umcf;
}


static char *
ngx_http_upstream_init_main_conf(ngx_conf_t *cf, void *conf)
{//初始化upstream模块的main_conf数据,在http{}指令解析完成之前会先create,完成之后init之
//下面只是将ngx_http_upstream_headers_in放入umcf->headers_in_hash里面。
    ngx_http_upstream_main_conf_t  *umcf = conf;

    ngx_uint_t                      i;
    ngx_array_t                     headers_in;
    ngx_hash_key_t                 *hk;
    ngx_hash_init_t                 hash;
    ngx_http_upstream_init_pt       init;
    ngx_http_upstream_header_t     *header;
    ngx_http_upstream_srv_conf_t  **uscfp;

    uscfp = umcf->upstreams.elts;
    for (i = 0; i < umcf->upstreams.nelts; i++) {//如果配置文件里面没有指定默认策略，则使用轮询策略。
        init = uscfp[i]->peer.init_upstream ? uscfp[i]->peer.init_upstream : ngx_http_upstream_init_round_robin;
        if (init(cf, uscfp[i]) != NGX_OK) {//调用之进行初始化。这个是指不同类型的轮询策略，哈希策略对应的初始化方法。
            return NGX_CONF_ERROR;
        }
    }

    /* upstream_headers_in_hash */
    if (ngx_array_init(&headers_in, cf->temp_pool, 32, sizeof(ngx_hash_key_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
	//下面将ngx_http_upstream_headers_in里面指定的HTTP头放入headers_in里面。
    for (header = ngx_http_upstream_headers_in; header->name.len; header++) {
        hk = ngx_array_push(&headers_in);
        if (hk == NULL) {
            return NGX_CONF_ERROR;
        }
        hk->key = header->name;
        hk->key_hash = ngx_hash_key_lc(header->name.data, header->name.len);
        hk->value = header;
    }
    hash.hash = &umcf->headers_in_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "upstream_headers_in_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;
	//初始化哈希表数据结构。
    if (ngx_hash_init(&hash, headers_in.elts, headers_in.nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
