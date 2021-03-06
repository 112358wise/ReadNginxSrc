
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_UPSTREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_event_pipe.h>
#include <ngx_http.h>


#define NGX_HTTP_UPSTREAM_FT_ERROR           0x00000002
#define NGX_HTTP_UPSTREAM_FT_TIMEOUT         0x00000004
#define NGX_HTTP_UPSTREAM_FT_INVALID_HEADER  0x00000008
#define NGX_HTTP_UPSTREAM_FT_HTTP_500        0x00000010
#define NGX_HTTP_UPSTREAM_FT_HTTP_502        0x00000020
#define NGX_HTTP_UPSTREAM_FT_HTTP_503        0x00000040
#define NGX_HTTP_UPSTREAM_FT_HTTP_504        0x00000080
#define NGX_HTTP_UPSTREAM_FT_HTTP_404        0x00000100
#define NGX_HTTP_UPSTREAM_FT_UPDATING        0x00000200
#define NGX_HTTP_UPSTREAM_FT_BUSY_LOCK       0x00000400
#define NGX_HTTP_UPSTREAM_FT_MAX_WAITING     0x00000800
#define NGX_HTTP_UPSTREAM_FT_NOLIVE          0x40000000
#define NGX_HTTP_UPSTREAM_FT_OFF             0x80000000

#define NGX_HTTP_UPSTREAM_FT_STATUS          (NGX_HTTP_UPSTREAM_FT_HTTP_500  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_502  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_503  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_504  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_404)

#define NGX_HTTP_UPSTREAM_INVALID_HEADER     40


#define NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT    0x00000002
#define NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES     0x00000004
#define NGX_HTTP_UPSTREAM_IGN_EXPIRES        0x00000008
#define NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL  0x00000010
#define NGX_HTTP_UPSTREAM_IGN_SET_COOKIE     0x00000020


typedef struct {
    ngx_msec_t                       bl_time;
    ngx_uint_t                       bl_state;

    ngx_uint_t                       status;
    time_t                           response_sec;
    ngx_uint_t                       response_msec;
    off_t                           response_length;

    ngx_str_t                       *peer;
} ngx_http_upstream_state_t;


typedef struct {//这个数组是每http{}块都有一份的。或者里面有的话也可以。Context: 	http，不能存在server里面的
    ngx_hash_t                       headers_in_hash;//ngx_http_upstream_headers_in里面的数据.
    ngx_array_t                      upstreams;//数组，代表有多少个upstream{}块。server xx.xx.xx.xx:xx weight=2 max_fails=3;  信息的数组。
    //由ngx_http_upstream_create_main_conf函数返回。存放在上层的ctx中。
                                             /* ngx_http_upstream_srv_conf_t */
} ngx_http_upstream_main_conf_t;

typedef struct ngx_http_upstream_srv_conf_s  ngx_http_upstream_srv_conf_t;

typedef ngx_int_t (*ngx_http_upstream_init_pt)(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
typedef ngx_int_t (*ngx_http_upstream_init_peer_pt)(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);


typedef struct {
    ngx_http_upstream_init_pt        init_upstream;//ngx_http_upstream_init_ip_hash函数等。默认为ngx_http_upstream_init_round_robin
    ngx_http_upstream_init_peer_pt   init;
    void                            *data;
} ngx_http_upstream_peer_t;


typedef struct {//一个server xx.xx.xx.xx:xx weight=2 max_fails=3;  的配置数据
    ngx_addr_t                      *addrs;
    ngx_uint_t                       naddrs;
    ngx_uint_t                       weight;
    ngx_uint_t                       max_fails;
    time_t                           fail_timeout;

    unsigned                         down:1;
    unsigned                         backup:1;
} ngx_http_upstream_server_t;


#define NGX_HTTP_UPSTREAM_CREATE        0x0001
#define NGX_HTTP_UPSTREAM_WEIGHT        0x0002
#define NGX_HTTP_UPSTREAM_MAX_FAILS     0x0004
#define NGX_HTTP_UPSTREAM_FAIL_TIMEOUT  0x0008
#define NGX_HTTP_UPSTREAM_DOWN          0x0010
#define NGX_HTTP_UPSTREAM_BACKUP        0x0020


struct ngx_http_upstream_srv_conf_s {//一个upstream{}配置结构的数据,这个是umcf->upstreams里面的数组项。umcf是upstream模块的顶层配置了。
    ngx_http_upstream_peer_t         peer;
    void                           **srv_conf;//我所属的upstream的ctx里面的srv_conf。回指一下我所属的配置数组ctx->srv_conf。

    ngx_array_t                     *servers;  /* ngx_http_upstream_server_t *///记录本upstream{}块的所有server指令。不是server{}块

    ngx_uint_t                       flags;
    ngx_str_t                        host;//表示这条upstream backend{}的backend，把它当做host，其实可以为任何字符串都行的。
    u_char                          *file_name;//配置文件名称
    ngx_uint_t                       line;//配置文件中的行号
    in_port_t                        port;//上面跟host对应，表示端口。
    in_port_t                        default_port;
};


typedef struct {
    ngx_http_upstream_srv_conf_t    *upstream;

    ngx_msec_t                       connect_timeout;
    ngx_msec_t                       send_timeout;
    ngx_msec_t                       read_timeout;
    ngx_msec_t                       timeout;

    size_t                           send_lowat;
    size_t                           buffer_size;

    size_t                           busy_buffers_size;
    size_t                           max_temp_file_size;
    size_t                           temp_file_write_size;

    size_t                           busy_buffers_size_conf;
    size_t                           max_temp_file_size_conf;
    size_t                           temp_file_write_size_conf;

    ngx_bufs_t                       bufs;

    ngx_uint_t                       ignore_headers;
    ngx_uint_t                       next_upstream;
    ngx_uint_t                       store_access;
    ngx_flag_t                       buffering;
    ngx_flag_t                       pass_request_headers;//是否要将HTTP请求头部的HEADER发送给后端，已HTTP_为前缀
    ngx_flag_t                       pass_request_body;//是否要发送请求的BODY，一般肯定要发送的

    ngx_flag_t                       ignore_client_abort;
    ngx_flag_t                       intercept_errors;
    ngx_flag_t                       cyclic_temp_file;

    ngx_path_t                      *temp_path;

    ngx_hash_t                       hide_headers_hash;
    ngx_array_t                     *hide_headers;
    ngx_array_t                     *pass_headers;

    ngx_addr_t                      *local;//用来绑定本地IP的，比如限制只从指定网卡发送数据给对端。

#if (NGX_HTTP_CACHE)
    ngx_shm_zone_t                  *cache;

    ngx_uint_t                       cache_min_uses;
    ngx_uint_t                       cache_use_stale;
    ngx_uint_t                       cache_methods;

    ngx_array_t                     *cache_valid;
    ngx_array_t                     *cache_bypass;
    ngx_array_t                     *no_cache;
#endif

    ngx_array_t                     *store_lengths;
    ngx_array_t                     *store_values;

    signed                           store:2;
    unsigned                         intercept_404:1;
    unsigned                         change_buffering:1;

#if (NGX_HTTP_SSL)
    ngx_ssl_t                       *ssl;
    ngx_flag_t                       ssl_session_reuse;
#endif

} ngx_http_upstream_conf_t;


typedef struct {
    ngx_str_t                        name;
    ngx_http_header_handler_pt       handler;//得到一个FCGI返回数据/或者一个代理模块返回行后，就会调用这个
    ngx_uint_t                       offset;
    ngx_http_header_handler_pt       copy_handler;
    ngx_uint_t                       conf;
    ngx_uint_t                       redirect;  /* unsigned   redirect:1; */
} ngx_http_upstream_header_t;//这个头部字段处理函数集中在ngx_http_upstream_headers_in里面设置


typedef struct {
    ngx_list_t                       headers;//保存了所有的将要传递给client的头
    ngx_uint_t                       status_n;//这里用来设置发送给client的 状态码
    ngx_str_t                        status_line;
	//下面这些头是为了更方便的存取值
    ngx_table_elt_t                 *status;
    ngx_table_elt_t                 *date;
    ngx_table_elt_t                 *server;
    ngx_table_elt_t                 *connection;

    ngx_table_elt_t                 *expires;
    ngx_table_elt_t                 *etag;
    ngx_table_elt_t                 *x_accel_expires;
    ngx_table_elt_t                 *x_accel_redirect;
    ngx_table_elt_t                 *x_accel_limit_rate;

    ngx_table_elt_t                 *content_type;
    ngx_table_elt_t                 *content_length;

    ngx_table_elt_t                 *last_modified;
    ngx_table_elt_t                 *location;
    ngx_table_elt_t                 *accept_ranges;
    ngx_table_elt_t                 *www_authenticate;

#if (NGX_HTTP_GZIP)
    ngx_table_elt_t                 *content_encoding;
#endif

    off_t                            content_length_n;

    ngx_array_t                      cache_control;
} ngx_http_upstream_headers_in_t;


typedef struct {
    ngx_str_t                        host;
    in_port_t                        port;
    ngx_uint_t                       no_port; /* unsigned no_port:1 */

    ngx_uint_t                       naddrs;
    in_addr_t                       *addrs;

    struct sockaddr                 *sockaddr;
    socklen_t                        socklen;

    ngx_resolver_ctx_t              *ctx;//ngx_resolver_ctx_t结构，指向解析的各项数据
} ngx_http_upstream_resolved_t;


typedef void (*ngx_http_upstream_handler_pt)(ngx_http_request_t *r,
    ngx_http_upstream_t *u);


struct ngx_http_upstream_s {//本结构体用来保存一个连接的upstream信息，包括各种需要upstream回调的函数等。
    ngx_http_upstream_handler_pt     read_event_handler; //ngx_http_upstream_process_header
    ngx_http_upstream_handler_pt     write_event_handler;// ngx_http_upstream_send_request_handler

    ngx_peer_connection_t            peer;

    ngx_event_pipe_t                *pipe;

    ngx_chain_t                     *request_bufs;//客户端发送过来的数据body部分，在ngx_http_upstream_init_request设置为客户端发送的HTTP BODY
    //也可能是代表要发送给后端的数据链表结构，比如ngx_http_proxy_create_request会这么放的。比如是FCGI结构数据，或者Proxy结构等。

    ngx_output_chain_ctx_t           output;//输出数据的结构，里面存有要发送的数据，以及发送的output_filter指针
    
    ngx_chain_writer_ctx_t           writer;//参考ngx_chain_writer，里面会将输出buf一个个连接到这里。
    //调用ngx_output_chain后，要发送的数据都会放在这里，然后发送，然后更新这个链表，指向剩下的还没有调用writev发送的。

    ngx_http_upstream_conf_t        *conf;//为u->conf = &flcf->upstream;

    ngx_http_upstream_headers_in_t   headers_in;//存放从上游返回的头部信息，

    ngx_http_upstream_resolved_t    *resolved;//解析出来的fastcgi_pass   127.0.0.1:9000;后面的字符串内容，可能有变量嘛。

    ngx_buf_t                        buffer;///读取上游返回的数据的缓冲区，也就是proxy，FCGI返回的数据。这里面有http头部，也可能有body部分。
    									//其body部分会跟event_pipe_t的preread_bufs结构对应起来。就是预读的buf，其实是i不小心读到的。
    size_t                           length;//要发送给客户端的数据大小，还需要读取这么多进来。

    ngx_chain_t                     *out_bufs;//这个是要发送给客户端的数据链接表�?
    ngx_chain_t                     *busy_bufs;//调用了ngx_http_output_filter，并将out_bufs的链表数据移动到这里，待发送完毕后，会移动到free_bufs
    ngx_chain_t                     *free_bufs;//空闲的缓冲区。可以分配

    ngx_int_t                      (*input_filter_init)(void *data);//进行初始化，没什么用，memcache设置为ngx_http_memcached_filter_init
    ngx_int_t                      (*input_filter)(void *data, ssize_t bytes);//用来读取后端的数据，非buffering模式。
    										//ngx_http_upstream_non_buffered_filter，ngx_http_memcached_filter等。
    										//这个函数的调用时机: ngx_http_upstream_process_non_buffered_upstream等调用ngx_unix_recv接收到upstream返回的数据后
    										//就调用这里进行协议转换，不过目前转换不多。
    
    void                            *input_filter_ctx;//指向所属的请求等上下文

#if (NGX_HTTP_CACHE)
    ngx_int_t                      (*create_key)(ngx_http_request_t *r);
#endif
	//下面的upstream回调指针是各个模块设置的，比如ngx_http_fastcgi_handler里面设置了fcgi的相关回调函数。
    ngx_int_t                      (*create_request)(ngx_http_request_t *r);//生成发送到上游服务器的请求缓冲（或者一条缓冲链）
    ngx_int_t                      (*reinit_request)(ngx_http_request_t *r);//在后端服务器被重置的情况下（在create_request被第二次调用之前）被调用
    ngx_int_t                      (*process_header)(ngx_http_request_t *r);//处理上游服务器回复的第一个bit，时常是保存一个指向上游回复负载的指针
    void                           (*abort_request)(ngx_http_request_t *r);//在客户端放弃请求的时候被调用
    void                           (*finalize_request)(ngx_http_request_t *r,//在Nginx完成从上游服务器读入回复以后被调用
                                         ngx_int_t rc);
    ngx_int_t                      (*rewrite_redirect)(ngx_http_request_t *r,
                                         ngx_table_elt_t *h, size_t prefix);

    ngx_msec_t                       timeout;

    ngx_http_upstream_state_t       *state;//当前的状态

    ngx_str_t                        method;//GET,HEAD,POST
    ngx_str_t                        schema;//就是前面的http,https,mecached://等。
    ngx_str_t                        uri;

    ngx_http_cleanup_pt             *cleanup;//ngx_http_upstream_cleanup

    unsigned                         store:1;
    unsigned                         cacheable:1;
    unsigned                         accel:1;
    unsigned                         ssl:1;
#if (NGX_HTTP_CACHE)
    unsigned                         cache_status:3;
#endif
    unsigned                         buffering:1;//是否要buffer 后端的数据，如果要用event_pipe的方式发送数据

    unsigned                         request_sent:1;//是否已经将request_bufs的数据放入输出链表里面
    unsigned                         header_sent:1;//标记已经发送了头部字段。
};


typedef struct {
    ngx_uint_t                      status;
    ngx_uint_t                      mask;
} ngx_http_upstream_next_t;


ngx_int_t ngx_http_upstream_header_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_http_upstream_create(ngx_http_request_t *r);
void ngx_http_upstream_init(ngx_http_request_t *r);
ngx_http_upstream_srv_conf_t *ngx_http_upstream_add(ngx_conf_t *cf,
    ngx_url_t *u, ngx_uint_t flags);
char *ngx_http_upstream_bind_set_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
ngx_int_t ngx_http_upstream_hide_headers_hash(ngx_conf_t *cf,
    ngx_http_upstream_conf_t *conf, ngx_http_upstream_conf_t *prev,
    ngx_str_t *default_hide_headers, ngx_hash_init_t *hash);


#define ngx_http_conf_upstream_srv_conf(uscf, module)                         \
    uscf->srv_conf[module.ctx_index]


extern ngx_module_t        ngx_http_upstream_module;
extern ngx_conf_bitmask_t  ngx_http_upstream_cache_method_mask[];
extern ngx_conf_bitmask_t  ngx_http_upstream_ignore_headers_masks[];


#endif /* _NGX_HTTP_UPSTREAM_H_INCLUDED_ */
