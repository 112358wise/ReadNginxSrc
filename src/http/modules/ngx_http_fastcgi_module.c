
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_http_upstream_conf_t       upstream;//upstream配置结构，用来存储配置信息的。u->conf = &flcf->upstream;

    ngx_str_t                      index;

    ngx_array_t                   *flushes;
    ngx_array_t                   *params_len;
    ngx_array_t                   *params;
    ngx_array_t                   *params_source;
    ngx_array_t                   *catch_stderr;

    ngx_array_t                   *fastcgi_lengths;//存放fcgi里面的脚本引擎，长度获取函数数组
    ngx_array_t                   *fastcgi_values;//数值拷贝数组

    ngx_hash_t                     headers_hash;
    ngx_uint_t                     header_params;

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t       cache_key;
#endif

#if (NGX_PCRE)
    ngx_regex_t                   *split_regex;
    ngx_str_t                      split_name;
#endif
} ngx_http_fastcgi_loc_conf_t;


typedef enum {
    ngx_http_fastcgi_st_version = 0,
    ngx_http_fastcgi_st_type,
    ngx_http_fastcgi_st_request_id_hi,
    ngx_http_fastcgi_st_request_id_lo,
    ngx_http_fastcgi_st_content_length_hi,
    ngx_http_fastcgi_st_content_length_lo,
    ngx_http_fastcgi_st_padding_length,
    ngx_http_fastcgi_st_reserved,
    ngx_http_fastcgi_st_data,
    ngx_http_fastcgi_st_padding
} ngx_http_fastcgi_state_e;


typedef struct {
    u_char                        *start;
    u_char                        *end;
} ngx_http_fastcgi_split_part_t;


typedef struct {
    ngx_http_fastcgi_state_e       state;
    u_char                        *pos;
    u_char                        *last;
    ngx_uint_t                     type;
    size_t                         length;
    size_t                         padding;

    unsigned                       fastcgi_stdout:1;
    unsigned                       large_stderr:1;

    ngx_array_t                   *split_parts;

    ngx_str_t                      script_name;
    ngx_str_t                      path_info;
} ngx_http_fastcgi_ctx_t;


#define NGX_HTTP_FASTCGI_RESPONDER      1

#define NGX_HTTP_FASTCGI_BEGIN_REQUEST  1
#define NGX_HTTP_FASTCGI_ABORT_REQUEST  2
#define NGX_HTTP_FASTCGI_END_REQUEST    3
#define NGX_HTTP_FASTCGI_PARAMS         4
#define NGX_HTTP_FASTCGI_STDIN          5
#define NGX_HTTP_FASTCGI_STDOUT         6
#define NGX_HTTP_FASTCGI_STDERR         7
#define NGX_HTTP_FASTCGI_DATA           8


typedef struct {
    u_char  version;
    u_char  type;
    u_char  request_id_hi;
    u_char  request_id_lo;
    u_char  content_length_hi;
    u_char  content_length_lo;
    u_char  padding_length;
    u_char  reserved;
} ngx_http_fastcgi_header_t;


typedef struct {
    u_char  role_hi;
    u_char  role_lo;
    u_char  flags;
    u_char  reserved[5];
} ngx_http_fastcgi_begin_request_t;


typedef struct {
    u_char  version;
    u_char  type;
    u_char  request_id_hi;
    u_char  request_id_lo;
} ngx_http_fastcgi_header_small_t;


typedef struct {//请求开始头包括正常头，加上开始请求的头部，
    ngx_http_fastcgi_header_t         h0;
    ngx_http_fastcgi_begin_request_t  br;
    ngx_http_fastcgi_header_small_t   h1;//这是什么�?莫非是下一个请求的参数部分的头部，预先追加在此?对，当为NGX_HTTP_FASTCGI_PARAMS模式时，后面直接追加KV
} ngx_http_fastcgi_request_start_t;


static ngx_int_t ngx_http_fastcgi_eval(ngx_http_request_t *r,
    ngx_http_fastcgi_loc_conf_t *flcf);
#if (NGX_HTTP_CACHE)
static ngx_int_t ngx_http_fastcgi_create_key(ngx_http_request_t *r);
#endif
static ngx_int_t ngx_http_fastcgi_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_fastcgi_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_fastcgi_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_fastcgi_input_filter(ngx_event_pipe_t *p,
    ngx_buf_t *buf);
static ngx_int_t ngx_http_fastcgi_process_record(ngx_http_request_t *r,
    ngx_http_fastcgi_ctx_t *f);
static void ngx_http_fastcgi_abort_request(ngx_http_request_t *r);
static void ngx_http_fastcgi_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static ngx_int_t ngx_http_fastcgi_add_variables(ngx_conf_t *cf);
static void *ngx_http_fastcgi_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_fastcgi_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_fastcgi_script_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_fastcgi_path_info_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_http_fastcgi_ctx_t *ngx_http_fastcgi_split(ngx_http_request_t *r,
    ngx_http_fastcgi_loc_conf_t *flcf);

static char *ngx_http_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_fastcgi_split_path_info(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char *ngx_http_fastcgi_store(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#if (NGX_HTTP_CACHE)
static char *ngx_http_fastcgi_cache(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_fastcgi_cache_key(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#endif

static char *ngx_http_fastcgi_lowat_check(ngx_conf_t *cf, void *post,
    void *data);


static ngx_conf_post_t  ngx_http_fastcgi_lowat_post =
    { ngx_http_fastcgi_lowat_check };


static ngx_conf_bitmask_t  ngx_http_fastcgi_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_header"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("http_500"), NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { ngx_string("http_503"), NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { ngx_string("http_404"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("updating"), NGX_HTTP_UPSTREAM_FT_UPDATING },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


ngx_module_t  ngx_http_fastcgi_module;


static ngx_command_t  ngx_http_fastcgi_commands[] = {

    { ngx_string("fastcgi_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("fastcgi_index"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, index),
      NULL },

    { ngx_string("fastcgi_split_path_info"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_split_path_info,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("fastcgi_store"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_store,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("fastcgi_store_access"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_conf_set_access_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.store_access),
      NULL },

    { ngx_string("fastcgi_ignore_client_abort"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.ignore_client_abort),
      NULL },

    { ngx_string("fastcgi_bind"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_bind_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.local),
      NULL },

    { ngx_string("fastcgi_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("fastcgi_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("fastcgi_send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.send_lowat),
      &ngx_http_fastcgi_lowat_post },

    { ngx_string("fastcgi_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("fastcgi_pass_request_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.pass_request_headers),
      NULL },

    { ngx_string("fastcgi_pass_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.pass_request_body),
      NULL },

    { ngx_string("fastcgi_intercept_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.intercept_errors),
      NULL },

    { ngx_string("fastcgi_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("fastcgi_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.bufs),
      NULL },

    { ngx_string("fastcgi_busy_buffers_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.busy_buffers_size_conf),
      NULL },

#if (NGX_HTTP_CACHE)

    { ngx_string("fastcgi_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_cache,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("fastcgi_cache_key"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_cache_key,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("fastcgi_cache_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_2MORE,
      ngx_http_file_cache_set_slot,
      0,
      0,
      &ngx_http_fastcgi_module },

    { ngx_string("fastcgi_cache_bypass"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_set_predicate_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_bypass),
      NULL },

    { ngx_string("fastcgi_no_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_set_predicate_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.no_cache),
      NULL },

    { ngx_string("fastcgi_cache_valid"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_file_cache_valid_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_valid),
      NULL },

    { ngx_string("fastcgi_cache_min_uses"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_min_uses),
      NULL },

    { ngx_string("fastcgi_cache_use_stale"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_use_stale),
      &ngx_http_fastcgi_next_upstream_masks },

    { ngx_string("fastcgi_cache_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_methods),
      &ngx_http_upstream_cache_method_mask },

#endif

    { ngx_string("fastcgi_temp_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
      ngx_conf_set_path_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.temp_path),
      NULL },

    { ngx_string("fastcgi_max_temp_file_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.max_temp_file_size_conf),
      NULL },

    { ngx_string("fastcgi_temp_file_write_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.temp_file_write_size_conf),
      NULL },

    { ngx_string("fastcgi_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.next_upstream),
      &ngx_http_fastcgi_next_upstream_masks },

    { ngx_string("fastcgi_param"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_keyval_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, params_source),
      NULL },

    { ngx_string("fastcgi_pass_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.pass_headers),
      NULL },

    { ngx_string("fastcgi_hide_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.hide_headers),
      NULL },

    { ngx_string("fastcgi_ignore_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.ignore_headers),
      &ngx_http_upstream_ignore_headers_masks },

    { ngx_string("fastcgi_catch_stderr"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, catch_stderr),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_fastcgi_module_ctx = {
    ngx_http_fastcgi_add_variables,        /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_fastcgi_create_loc_conf,      /* create location configuration */
    ngx_http_fastcgi_merge_loc_conf        /* merge location configuration */
};


ngx_module_t  ngx_http_fastcgi_module = {
    NGX_MODULE_V1,
    &ngx_http_fastcgi_module_ctx,          /* module context */
    ngx_http_fastcgi_commands,             /* module directives */
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

//擦，请求的ID直接设置为1，不复用了。太奢侈了
static ngx_http_fastcgi_request_start_t  ngx_http_fastcgi_request_start = {
    { 1,                                               /* version */
      NGX_HTTP_FASTCGI_BEGIN_REQUEST,                  /* type */
      0,                                               /* request_id_hi */
      1,                                               /* request_id_lo */
      0,                                               /* content_length_hi */
      sizeof(ngx_http_fastcgi_begin_request_t),        /* content_length_lo */
      0,                                               /* padding_length */
      0 },                                             /* reserved */

    { 0,                                               /* role_hi */
      NGX_HTTP_FASTCGI_RESPONDER,                      /* role_lo */
      0, /* NGX_HTTP_FASTCGI_KEEP_CONN */              /* flags */
      { 0, 0, 0, 0, 0 } },                             /* reserved[5] */
//下面是为了发送参数预先追加的头部。
    { 1,                                               /* version */
      NGX_HTTP_FASTCGI_PARAMS,                         /* type */
      0,                                               /* request_id_hi */
      1 },                                             /* request_id_lo */

};


static ngx_http_variable_t  ngx_http_fastcgi_vars[] = {

    { ngx_string("fastcgi_script_name"), NULL,
      ngx_http_fastcgi_script_name_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_string("fastcgi_path_info"), NULL,
      ngx_http_fastcgi_path_info_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};


static ngx_str_t  ngx_http_fastcgi_hide_headers[] = {
    ngx_string("Status"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};


#if (NGX_HTTP_CACHE)

static ngx_keyval_t  ngx_http_fastcgi_cache_headers[] = {
    { ngx_string("HTTP_IF_MODIFIED_SINCE"), ngx_string("") },
    { ngx_string("HTTP_IF_UNMODIFIED_SINCE"), ngx_string("") },
    { ngx_string("HTTP_IF_NONE_MATCH"), ngx_string("") },
    { ngx_string("HTTP_IF_MATCH"), ngx_string("") },
    { ngx_string("HTTP_RANGE"), ngx_string("") },
    { ngx_string("HTTP_IF_RANGE"), ngx_string("") },
    { ngx_null_string, ngx_null_string }
};

#endif


static ngx_path_init_t  ngx_http_fastcgi_temp_path = {
    ngx_string(NGX_HTTP_FASTCGI_TEMP_PATH), { 1, 2, 0 }
};


static ngx_int_t
ngx_http_fastcgi_handler(ngx_http_request_t *r)
{//FCGI处理入口,ngx_http_core_run_phases里面当做一个内容处理模块调用的。
//ngx_http_core_find_config_phase里面的ngx_http_update_location_config设置
    ngx_int_t                     rc;
    ngx_http_upstream_t          *u;
    ngx_http_fastcgi_ctx_t       *f;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    if (r->subrequest_in_memory) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "ngx_http_fastcgi_module does not support subrequest in memory");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_upstream_create(r) != NGX_OK) {//创建一个ngx_http_upstream_t结构，放到r->upstream里面去。
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    f = ngx_pcalloc(r->pool, sizeof(ngx_http_fastcgi_ctx_t));
    if (f == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_http_set_ctx(r, f, ngx_http_fastcgi_module);//r->ctx[module.ctx_index] = c;也就是将申请的fcgi_ctx_t放到这个请求的ctx里面
    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);//得到fcgi的配置。(r)->loc_conf[module.ctx_index]
    if (flcf->fastcgi_lengths) {//如果这个fcgi有变量，那么久需要解析一下变量。
        if (ngx_http_fastcgi_eval(r, flcf) != NGX_OK) {//计算fastcgi_pass   127.0.0.1:9000;后面的URL的内容。也就是域名解析；
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }
    u = r->upstream;
    ngx_str_set(&u->schema, "fastcgi://");//用fcgi协议。
    u->output.tag = (ngx_buf_tag_t) &ngx_http_fastcgi_module;
    u->conf = &flcf->upstream;
#if (NGX_HTTP_CACHE)
    u->create_key = ngx_http_fastcgi_create_key;//根据flcf->cache_key里面的复杂表达式计算 scgi_cache_key line;指令后面的复杂表达式line;
#endif
    u->create_request = ngx_http_fastcgi_create_request;
    u->reinit_request = ngx_http_fastcgi_reinit_request;
    u->process_header = ngx_http_fastcgi_process_header;
    u->abort_request = ngx_http_fastcgi_abort_request;
    u->finalize_request = ngx_http_fastcgi_finalize_request;
	
	//下面的数据结构是给event_pipe用的，用来对FCGI的数据进行buffering处理的。FCGI写死为buffering
    u->buffering = 1;
    u->pipe = ngx_pcalloc(r->pool, sizeof(ngx_event_pipe_t));
    if (u->pipe == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	//设置读取fcgi协议格式数据的回调，当解析完带有\r\n\r\n的头部的FCGI包后，后面的包解析都由这个函数进行处理。
    u->pipe->input_filter = ngx_http_fastcgi_input_filter;
    u->pipe->input_ctx = r;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    return NGX_DONE;
}


static ngx_int_t
ngx_http_fastcgi_eval(ngx_http_request_t *r, ngx_http_fastcgi_loc_conf_t *flcf)
{//计算fastcgi_pass   127.0.0.1:9000;后面的URL内容，设置到u->resolved上面去
    ngx_url_t             url;
    ngx_http_upstream_t  *u;

    ngx_memzero(&url, sizeof(ngx_url_t));
    if (ngx_http_script_run(r, &url.url, flcf->fastcgi_lengths->elts, 0,flcf->fastcgi_values->elts) == NULL) {
        return NGX_ERROR;//根据lcodes和codes计算目标字符串的内容、目标字符串结果存放在value->data;里面，也就是url.url
    }
    url.no_resolve = 1;
    if (ngx_parse_url(r->pool, &url) != NGX_OK) {//对u参数里面的url,unix,inet6等地址进行简析；
         if (url.err) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, upstream \"%V\"", url.err, &url.url);
        }
        return NGX_ERROR;
    }
    if (url.no_port) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,  "no port in upstream \"%V\"", &url.url);
        return NGX_ERROR;
    }
    u = r->upstream;
    u->resolved = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_resolved_t));
    if (u->resolved == NULL) {
        return NGX_ERROR;
    }

    if (url.addrs && url.addrs[0].sockaddr) {
		//将解析出来的地址保存，在upstream里面急不需要进行与解析了。
        u->resolved->sockaddr = url.addrs[0].sockaddr;
        u->resolved->socklen = url.addrs[0].socklen;
        u->resolved->naddrs = 1;
        u->resolved->host = url.addrs[0].name;
    } else {
        u->resolved->host = url.host;
        u->resolved->port = url.port;
    }
    return NGX_OK;
}


#if (NGX_HTTP_CACHE)

static ngx_int_t
ngx_http_fastcgi_create_key(ngx_http_request_t *r)
{//根据之前在解析scgi_cache_key指令的时候计算出来的复杂表达式结构，存放在flcf->cache_key中的，计算出cache_key。
    ngx_str_t                   *key;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    key = ngx_array_push(&r->cache->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }
    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);
	//根据&flcf->cache_key复杂表达式结构，获取其代表的目标值，存入key.
    if (ngx_http_complex_value(r, &flcf->cache_key, key) != NGX_OK) {
        return NGX_ERROR;
    }
    return NGX_OK;
}

#endif


static ngx_int_t
ngx_http_fastcgi_create_request(ngx_http_request_t *r)
{//设置FCGI的各种请求开始，请求头部，HTTP BODY数据部分的拷贝，参数拷贝等。后面基本就可以发送数据了
//存放在u->request_bufs链接表里面。
    off_t                         file_pos;
    u_char                        ch, *pos, *lowcase_key;
    size_t                        size, len, key_len, val_len, padding,
                                  allocated;
    ngx_uint_t                    i, n, next, hash, header_params;
    ngx_buf_t                    *b;
    ngx_chain_t                  *cl, *body;
    ngx_list_part_t              *part;
    ngx_table_elt_t              *header, **ignored;
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t      e, le;
    ngx_http_fastcgi_header_t    *h;
    ngx_http_fastcgi_loc_conf_t  *flcf;
    ngx_http_script_len_code_pt   lcode;

    len = 0;
    header_params = 0;
    ignored = NULL;
    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    if (flcf->params_len) {//处理追加的FCGI参数，如fastcgi_param  SCRIPT_FILENAME    $document_root$fastcgi_script_name;
        ngx_memzero(&le, sizeof(ngx_http_script_engine_t));
        ngx_http_script_flush_no_cacheable_variables(r, flcf->flushes);
        le.flushed = 1;
        le.ip = flcf->params_len->elts;
        le.request = r;

        while (*(uintptr_t *) le.ip) {
            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            key_len = lcode(&le);
            for (val_len = 0; *(uintptr_t *) le.ip; val_len += lcode(&le)) {
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
            }
            le.ip += sizeof(uintptr_t);
            len += 1 + key_len + ((val_len > 127) ? 4 : 1) + val_len;
        }
    }

    if (flcf->upstream.pass_request_headers) {////是否要将HTTP请求头部的HEADER发送给后端，已HTTP_为前缀
        allocated = 0;
        lowcase_key = NULL;
        if (flcf->header_params) {
            ignored = ngx_palloc(r->pool, flcf->header_params * sizeof(void *));
            if (ignored == NULL) {
                return NGX_ERROR;
            }
        }

        part = &r->headers_in.headers.part;//拿到请求的头部HTTP HEADER部分，一个个遍历。
        header = part->elts;//请求的HTTP HEADER K-V队列
        for (i = 0; /* void */; i++) {
            if (i >= part->nelts) {//如果太大了，下一个从2开始
                if (part->next == NULL) {
                    break;//一组一组的，如果没有新组了，就退出，否则继续。
                }
                part = part->next;
                header = part->elts;
                i = 0;
            }

            if (flcf->header_params) {
                if (allocated < header[i].key.len) {
                    allocated = header[i].key.len + 16;
                    lowcase_key = ngx_pnalloc(r->pool, allocated);
                    if (lowcase_key == NULL) {
                        return NGX_ERROR;
                    }
                }

                hash = 0;

                for (n = 0; n < header[i].key.len; n++) {
                    ch = header[i].key.data[n];//拿到头部KEY，转换为小写
                    if (ch >= 'A' && ch <= 'Z') {
                        ch |= 0x20;
                    } else if (ch == '-') {
                        ch = '_';
                    }
                    hash = ngx_hash(hash, ch);
                    lowcase_key[n] = ch;
                }
                if (ngx_hash_find(&flcf->headers_hash, hash, lowcase_key, n)) {
                    ignored[header_params++] = &header[i];//这是啥意思�
                    continue;
                }
                n += sizeof("HTTP_") - 1;
            } else {
                n = sizeof("HTTP_") - 1 + header[i].key.len;//加上这个http头部的长度，以及HTTP_长度
            }//计算这个HTTP头部所需要的FCGI大小。KEY太长了或者VALUE太长了都需要4个字节存储。否则各1个字节。这是FCGI的规范
            len += ((n > 127) ? 4 : 1) + ((header[i].value.len > 127) ? 4 : 1) + n + header[i].value.len;//累加长度。
        }
    }
    if (len > 65535) {//擦，太长了。最多16K的头部字段。
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,"fastcgi request record is too big: %uz", len);
        return NGX_ERROR;
    }

    padding = 8 - len % 8;//计算后面的填充字节
    padding = (padding == 8) ? 0 : padding;

    size = sizeof(ngx_http_fastcgi_header_t)//一个标准的FCGI头部。type为NGX_HTTP_FASTCGI_BEGIN_REQUEST
           + sizeof(ngx_http_fastcgi_begin_request_t)//以及后面紧跟的开始请求头。

           + sizeof(ngx_http_fastcgi_header_t)  /* NGX_HTTP_FASTCGI_PARAMS *///参数头部，里面的type为NGX_HTTP_FASTCGI_PARAMS
           + len + padding //加上K:V的参数字节，K-V对，K-V前面为Key长度，value长度。
           + sizeof(ngx_http_fastcgi_header_t)  /* NGX_HTTP_FASTCGI_PARAMS *///参数发送的结尾。应该是全0的。

           + sizeof(ngx_http_fastcgi_header_t); /* NGX_HTTP_FASTCGI_STDIN *///这个就是请求的数据了，也就是BODY部分。但BODY啥时候发送的呢

    b = ngx_create_temp_buf(r->pool, size);//先申请头部大小。
    if (b == NULL) {
        return NGX_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;//将头部放入缓冲链表此处。
    ngx_memcpy(b->pos, &ngx_http_fastcgi_request_start, sizeof(ngx_http_fastcgi_request_start_t));//直接拷贝默认的FCGI头部字节，以及参数部分的头部
	//h 跳过标准的请求头部，跳到参数开始头部。NGX_HTTP_FASTCGI_PARAMS部分
    h = (ngx_http_fastcgi_header_t *) (b->pos + sizeof(ngx_http_fastcgi_header_t) + sizeof(ngx_http_fastcgi_begin_request_t));

    h->content_length_hi = (u_char) ((len >> 8) & 0xff);//请求长度。怎么不包括HTTP BODY了，哦，BODY应该是在NGX_HTTP_FASTCGI_STDIN里面发送。
    h->content_length_lo = (u_char) (len & 0xff);//设置这次的请求的content_length为我要发送的K-V对的长度。
    h->padding_length = (u_char) padding;//以及填充字节
    h->reserved = 0;//下面标记头部已经使用的长度。
    b->last = b->pos + sizeof(ngx_http_fastcgi_header_t) + sizeof(ngx_http_fastcgi_begin_request_t) + sizeof(ngx_http_fastcgi_header_t);
	//现在b->last指向参数部分的开头，跳过第一个参数头部。因为其数据已经设置，如上。

    if (flcf->params_len) {//处理FCGI的参数，进行相关的拷贝操作。
        ngx_memzero(&e, sizeof(ngx_http_script_engine_t));
        e.ip = flcf->params->elts;
        e.pos = b->last;//FCGI的参数先紧跟后面追加
        e.request = r;
        e.flushed = 1;

        le.ip = flcf->params_len->elts;//一个个FCGI设置的参数到后面fastcgi_param

        while (*(uintptr_t *) le.ip) {
            lcode = *(ngx_http_script_len_code_pt *) le.ip;//为ngx_http_script_copy_len_code，得到脚本长度。
            key_len = (u_char) lcode(&le);//得到这个FCGI的参数长度，并且ip后移

            for (val_len = 0; *(uintptr_t *) le.ip; val_len += lcode(&le)) {//ngx_http_script_copy_var_len_code拷贝KEY
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
            }
            le.ip += sizeof(uintptr_t);//函数指针往后移动，到拷贝字符串的步骤
            *e.pos++ = (u_char) key_len;//KEY长度

            if (val_len > 127) {
                *e.pos++ = (u_char) (((val_len >> 24) & 0x7f) | 0x80);
                *e.pos++ = (u_char) ((val_len >> 16) & 0xff);
                *e.pos++ = (u_char) ((val_len >> 8) & 0xff);
                *e.pos++ = (u_char) (val_len & 0xff);

            } else {
                *e.pos++ = (u_char) val_len;//VALUE长度。
            }

            while (*(uintptr_t *) e.ip) {
                code = *(ngx_http_script_code_pt *) e.ip;//ngx_http_script_copy_code
                code((ngx_http_script_engine_t *) &e);
            }
            e.ip += sizeof(uintptr_t);//再跳一个指针长度
            ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"fastcgi param: \"%*s: %*s\"", key_len, e.pos - (key_len + val_len), val_len, e.pos - val_len);
        }
        b->last = e.pos;
    }//处理完fastcgi_param  SCRIPT_FILENAME    $document_root$fastcgi_script_name;参数

    if (flcf->upstream.pass_request_headers) {//是否要将HTTP请求头部的HEADER发送给后端，已HTTP_为前缀
        part = &r->headers_in.headers.part;
        header = part->elts;//下面将HTTP头部一个个大写后发送过去。
        for (i = 0; /* void */; i++) {
            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                header = part->elts;
                i = 0;
            }
            for (n = 0; n < header_params; n++) {
                if (&header[i] == ignored[n]) {
                    goto next;
                }
            }
            key_len = sizeof("HTTP_") - 1 + header[i].key.len;
            if (key_len > 127) {
                *b->last++ = (u_char) (((key_len >> 24) & 0x7f) | 0x80);
                *b->last++ = (u_char) ((key_len >> 16) & 0xff);
                *b->last++ = (u_char) ((key_len >> 8) & 0xff);
                *b->last++ = (u_char) (key_len & 0xff);

            } else {
                *b->last++ = (u_char) key_len;
            }
            val_len = header[i].value.len;
            if (val_len > 127) {
                *b->last++ = (u_char) (((val_len >> 24) & 0x7f) | 0x80);
                *b->last++ = (u_char) ((val_len >> 16) & 0xff);
                *b->last++ = (u_char) ((val_len >> 8) & 0xff);
                *b->last++ = (u_char) (val_len & 0xff);
            } else {
                *b->last++ = (u_char) val_len;
            }
            b->last = ngx_cpymem(b->last, "HTTP_", sizeof("HTTP_") - 1);
            for (n = 0; n < header[i].key.len; n++) {
                ch = header[i].key.data[n];
                if (ch >= 'a' && ch <= 'z') {
                    ch &= ~0x20;
                } else if (ch == '-') {
                    ch = '_';
                }
                *b->last++ = ch;
            }
            b->last = ngx_copy(b->last, header[i].value.data, val_len);
            ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "fastcgi param: \"%*s: %*s\"",
                           key_len, b->last - (key_len + val_len), val_len, b->last - val_len);
        next:
            continue;
        }
    }

    if (padding) {
        ngx_memzero(b->last, padding);
        b->last += padding;
    }
    h = (ngx_http_fastcgi_header_t *) b->last;//K-V也添加完毕了，后面是啥了呢，是FCGI的参数部分的结尾，规定每个必须有全0的结尾。
    b->last += sizeof(ngx_http_fastcgi_header_t);//跳到下一个HEADER，为NGX_HTTP_FASTCGI_STDIN，也就是发送数据部分。

    h->version = 1;
    h->type = NGX_HTTP_FASTCGI_PARAMS;//标记为参数部分的头，且下面的内容为空，表示是结尾。
    h->request_id_hi = 0;
    h->request_id_lo = 1;
    h->content_length_hi = 0;
    h->content_length_lo = 0;
    h->padding_length = 0;
    h->reserved = 0;

    h = (ngx_http_fastcgi_header_t *) b->last;//得到下一个要处理的头部，也就是NGX_HTTP_FASTCGI_STDIN部分。
    b->last += sizeof(ngx_http_fastcgi_header_t);
    if (flcf->upstream.pass_request_body) {//是否要发送请求的BODY，一般肯定要发送的
        body = r->upstream->request_bufs;//这个有数据了吗，有的，在ngx_http_upstream_init_request开头设置的。设置为客户端发送的HTTP BODY
        r->upstream->request_bufs = cl;//指向新的，刚刚分配的缓冲链接结构。下面将BODY拷贝到cl里面
#if (NGX_SUPPRESS_WARN)
        file_pos = 0;
        pos = NULL;
#endif
        while (body) {//为什么之前会有数据呢
            if (body->buf->in_file) {//如果在文件里面
                file_pos = body->buf->file_pos;
            } else {
                pos = body->buf->pos;
            }
            next = 0;
            do {
                b = ngx_alloc_buf(r->pool);//申请一块ngx_buf_s元数据结构
                if (b == NULL) {
                    return NGX_ERROR;
                }
                ngx_memcpy(b, body->buf, sizeof(ngx_buf_t));//拷贝元数据
                if (body->buf->in_file) {
                    b->file_pos = file_pos;
                    file_pos += 32 * 1024;//一次32K的大小。
                    if (file_pos >= body->buf->file_last) {
                        file_pos = body->buf->file_last;
                        next = 1;
                    }
                    b->file_last = file_pos;
                    len = (ngx_uint_t) (file_pos - b->file_pos);
                } else {
                    b->pos = pos;
                    pos += 32 * 1024;
                    if (pos >= body->buf->last) {
                        pos = body->buf->last;
                        next = 1;
                    }
                    b->last = pos;
                    len = (ngx_uint_t) (pos - b->pos);
                }

                padding = 8 - len % 8;
                padding = (padding == 8) ? 0 : padding;

                h->version = 1;
                h->type = NGX_HTTP_FASTCGI_STDIN;//发送BODY部分
                h->request_id_hi = 0;
                h->request_id_lo = 1;//NGINX 永远只用了1个。
                h->content_length_hi = (u_char) ((len >> 8) & 0xff);//说明NGINX对于BODY是一块块发送的，不一定是一次发送。
                h->content_length_lo = (u_char) (len & 0xff);
                h->padding_length = (u_char) padding;
                h->reserved = 0;
                cl->next = ngx_alloc_chain_link(r->pool);//申请一个新的链接结构，存放这块BODY，参数啥的存放在第一块BODY部分啦
                if (cl->next == NULL) {
                    return NGX_ERROR;
                }
                cl = cl->next;
                cl->buf = b;//设置这块新的连接结构的数据为刚刚的部分BODY内容。
                b = ngx_create_temp_buf(r->pool, sizeof(ngx_http_fastcgi_header_t) + padding);//创建一个新的头部缓冲，存放头部的数据，以及填充字节
                if (b == NULL) {
                    return NGX_ERROR;
                }
                if (padding) {
                    ngx_memzero(b->last, padding);
                    b->last += padding;
                }
                h = (ngx_http_fastcgi_header_t *) b->last;//h指向下一个头部啦。如果没有就设置为结尾呗。
                b->last += sizeof(ngx_http_fastcgi_header_t);

                cl->next = ngx_alloc_chain_link(r->pool);
                if (cl->next == NULL) {
                    return NGX_ERROR;
                }
                cl = cl->next;
                cl->buf = b;//将这个下一个头部的缓冲区放入链接表。好吧，这个链接表算长的了。
            } while (!next);

            body = body->next;//下一块BODY数据
        }//结尾: while (body) {//为什么之前会有数据呢

    } else {//如果不用发送请求的BODY部分。直接使用刚才的链接表就行。不用拷贝BODY了
        r->upstream->request_bufs = cl;
    }

    h->version = 1;
    h->type = NGX_HTTP_FASTCGI_STDIN;//老规矩，一种类型结尾来一个全0的头部。
    h->request_id_hi = 0;
    h->request_id_lo = 1;
    h->content_length_hi = 0;
    h->content_length_lo = 0;
    h->padding_length = 0;
    h->reserved = 0;

    cl->next = NULL;//结尾了、
    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_reinit_request(ngx_http_request_t *r)
{
    ngx_http_fastcgi_ctx_t  *f;

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (f == NULL) {
        return NGX_OK;
    }

    f->state = ngx_http_fastcgi_st_version;
    f->fastcgi_stdout = 0;
    f->large_stderr = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_process_header(ngx_http_request_t *r)
{//解析FCGI的请求返回记录，如果是返回标准输出，则解析其请求的HTTP头部并回调其头部数据的回调。数据部分还没有解析。
//ngx_http_upstream_process_header会每次读取数据后，调用这里。
//请注意这个函数执行完，不一定是所有BODY数据也读取完毕了，可能是包含HTTP HEADER的某个FCGI包读取完毕了，然后进行解析的时候
//ngx_http_parse_header_line函数碰到了\r\n\r\n于是返回NGX_HTTP_PARSE_HEADER_DONE，然后本函数就执行完成。
    u_char                         *p, *msg, *start, *last,
                                   *part_start, *part_end;
    size_t                          size;
    ngx_str_t                      *status_line, *pattern;
    ngx_int_t                       rc, status;
    ngx_buf_t                       buf;
    ngx_uint_t                      i;
    ngx_table_elt_t                *h;
    ngx_http_upstream_t            *u;
    ngx_http_fastcgi_ctx_t         *f;
    ngx_http_upstream_header_t     *hh;
    ngx_http_fastcgi_loc_conf_t    *flcf;
    ngx_http_fastcgi_split_part_t  *part;
    ngx_http_upstream_main_conf_t  *umcf;

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);
    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    u = r->upstream;

    for ( ;; ) {
        if (f->state < ngx_http_fastcgi_st_data) {//上次的状态都没有读完一个头部,先解析这些头部看看是不是有问题。
            f->pos = u->buffer.pos;
            f->last = u->buffer.last;
            rc = ngx_http_fastcgi_process_record(r, f);//简单处理了一下FCGI的请求头部，没有解析头部的类型含义，以及其里面的协议
            u->buffer.pos = f->pos;
            u->buffer.last = f->last;
            if (rc == NGX_AGAIN) {//如果还没处理完这个头部。则返回待继续
                return NGX_AGAIN;
            }
            if (rc == NGX_ERROR) {//头部错误
                return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
            if (f->type != NGX_HTTP_FASTCGI_STDOUT && f->type != NGX_HTTP_FASTCGI_STDERR)
            {//既不是标准输出，又不是错误，那是啥� 请求结束不会这么快到来的
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent unexpected FastCGI record: %d", f->type);
                return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
            if (f->type == NGX_HTTP_FASTCGI_STDOUT && f->length == 0) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream closed prematurely FastCGI stdout");
                return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
        }
        if (f->state == ngx_http_fastcgi_st_padding) {//上次的状态是读取填充字节
            if (u->buffer.pos + f->padding < u->buffer.last) {//如果填充数据已经读完了就跳到处理后面的版本的状态。
                f->state = ngx_http_fastcgi_st_version;//ngx_http_fastcgi_header_t这个结构后面的版本字段
                u->buffer.pos += f->padding;
                continue;
            }
            if (u->buffer.pos + f->padding == u->buffer.last) {
                f->state = ngx_http_fastcgi_st_version;//正好读了这么多，没有数据了，后面没有版本了。
                u->buffer.pos = u->buffer.last;
                return NGX_AGAIN;//返回，回到ngx_http_upstream_process_header，到时候会再读取一点，然后处理。
            }
            f->padding -= u->buffer.last - u->buffer.pos;
            u->buffer.pos = u->buffer.last;
            return NGX_AGAIN;//返回继续读取
        }
//到这里，表示一个请求的头部已经读取完毕。下面可以进行分析了
        /* f->state == ngx_http_fastcgi_st_data */
        if (f->type == NGX_HTTP_FASTCGI_STDERR) {//出问题了
            if (f->length) {//如果是标准错误输出头，则length记录器数据长度，也就是content长度
                msg = u->buffer.pos;
                if (u->buffer.pos + f->length <= u->buffer.last) {
                    u->buffer.pos += f->length;
                    f->length = 0;//跳过这些数据
                    f->state = ngx_http_fastcgi_st_padding;
                } else {
                    f->length -= u->buffer.last - u->buffer.pos;
                    u->buffer.pos = u->buffer.last;
                }
                for (p = u->buffer.pos - 1; msg < p; p--) {
                    if (*p != LF && *p != CR && *p != '.' && *p != ' ') {
                        break;
                    }
                }
                p++;
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "FastCGI sent in stderr: \"%*s\"", p - msg, msg);
                flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);
                if (flcf->catch_stderr) {
                    pattern = flcf->catch_stderr->elts;
                    for (i = 0; i < flcf->catch_stderr->nelts; i++) {
                        if (ngx_strnstr(msg, (char *) pattern[i].data, p - msg) != NULL) {
                            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
                        }
                    }
                }
                if (u->buffer.pos == u->buffer.last) {
                    if (!f->fastcgi_stdout) {
                        /*
                         * the special handling the large number
                         * of the PHP warnings to not allocate memory
                         */
#if (NGX_HTTP_CACHE)
                        if (r->cache) {
                            u->buffer.pos = u->buffer.start + r->cache->header_start;
                        } else {
                            u->buffer.pos = u->buffer.start;
                        }
#endif
                        u->buffer.last = u->buffer.pos;
                        f->large_stderr = 1;
                    }
                    return NGX_AGAIN;
                }
            } else {
                f->state = ngx_http_fastcgi_st_version;
            }
            continue;
        }
        /* f->type == NGX_HTTP_FASTCGI_STDOUT *///不是NGX_HTTP_FASTCGI_STDERR，那就只有标准输出了�
#if (NGX_HTTP_CACHE)
        if (f->large_stderr && r->cache) {
            u_char                     *start;
            ssize_t                     len;
            ngx_http_fastcgi_header_t  *fh;
            start = u->buffer.start + r->cache->header_start;
            len = u->buffer.pos - start - 2 * sizeof(ngx_http_fastcgi_header_t);
            /*
             * A tail of large stderr output before HTTP header is placed
             * in a cache file without a FastCGI record header.
             * To workaround it we put a dummy FastCGI record header at the
             * start of the stderr output or update r->cache_header_start,
             * if there is no enough place for the record header.
             */
            if (len >= 0) {
                fh = (ngx_http_fastcgi_header_t *) start;
                fh->version = 1;
                fh->type = NGX_HTTP_FASTCGI_STDERR;
                fh->request_id_hi = 0;
                fh->request_id_lo = 1;
                fh->content_length_hi = (u_char) ((len >> 8) & 0xff);
                fh->content_length_lo = (u_char) (len & 0xff);
                fh->padding_length = 0;
                fh->reserved = 0;
            } else {
                r->cache->header_start += u->buffer.pos - start  - sizeof(ngx_http_fastcgi_header_t);
            }
            f->large_stderr = 0;
        }
#endif
        f->fastcgi_stdout = 1;
        start = u->buffer.pos;
        if (u->buffer.pos + f->length < u->buffer.last) {//如果已经读取了指定长度的数据，下面就直接设置头部长度，跳过数据部分。
            /*//这个干嘛呢，将last指向头部后面的数据部分。
             * set u->buffer.last to the end of the FastCGI record data for ngx_http_parse_header_line()
             */
            last = u->buffer.last;//数据尾部。
            u->buffer.last = u->buffer.pos + f->length;
        } else {
            last = NULL;
        }
        for ( ;; ) {//这个循环不断解析HTTP请求行的数据
            part_start = u->buffer.pos;
            part_end = u->buffer.last;
            rc = ngx_http_parse_header_line(r, &u->buffer, 1);//解析请求的HTTP HEADER部分，每次一行。
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http fastcgi parser: %d", rc);
            if (rc == NGX_AGAIN) {
                break;
            }
            if (rc == NGX_OK) {
                /* a header line has been parsed successfully *///解析到了一行数据了。
                h = ngx_list_push(&u->headers_in.headers);
                if (h == NULL) {
                    return NGX_ERROR;
                }
                if (f->split_parts && f->split_parts->nelts) {//如果之前是一段段头部数据分析的，则现在需要组合在一起，然后再次解析。
                    part = f->split_parts->elts;
                    size = u->buffer.pos - part_start;//得到这段HEADER的长度
                    for (i = 0; i < f->split_parts->nelts; i++) {
                        size += part[i].end - part[i].start;//加上每个数组元素的长度。
                    }
                    p = ngx_pnalloc(r->pool, size);//申请这么多内存
                    if (p == NULL) {
                        return NGX_ERROR;
                    }
                    buf.pos = p;
                    for (i = 0; i < f->split_parts->nelts; i++) {
                        p = ngx_cpymem(p, part[i].start,part[i].end - part[i].start);//将split_parts中的数据拷贝过来。
                    }
                    p = ngx_cpymem(p, part_start, u->buffer.pos - part_start);//这个新的头部也拷贝过来。
                    buf.last = p;

                    f->split_parts->nelts = 0;

                    rc = ngx_http_parse_header_line(r, &buf, 1);

                    h->key.len = r->header_name_end - r->header_name_start;
                    h->key.data = r->header_name_start;
                    h->key.data[h->key.len] = '\0';

                    h->value.len = r->header_end - r->header_start;
                    h->value.data = r->header_start;
                    h->value.data[h->value.len] = '\0';

                    h->lowcase_key = ngx_pnalloc(r->pool, h->key.len);
                    if (h->lowcase_key == NULL) {
                        return NGX_ERROR;
                    }
                } else {//否则数据里面就是头部数据了。
                    h->key.len = r->header_name_end - r->header_name_start;
                    h->value.len = r->header_end - r->header_start;
                    h->key.data = ngx_pnalloc(r->pool, h->key.len + 1 + h->value.len + 1 + h->key.len);
                    if (h->key.data == NULL) {
                        return NGX_ERROR;
                    }
                    h->value.data = h->key.data + h->key.len + 1;
                    h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;
                    ngx_cpystrn(h->key.data, r->header_name_start,  h->key.len + 1);
                    ngx_cpystrn(h->value.data, r->header_start, h->value.len + 1);
                }

                h->hash = r->header_hash;
                if (h->key.len == r->lowcase_index) {
                    ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);
                } else {
                    ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
                }
                hh = ngx_hash_find(&umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
                if (hh && hh->handler(r, h, hh->offset) != NGX_OK) {//回调对应的HEADER处理函数。集中在这ngx_http_upstream_headers_in
                    return NGX_ERROR;
                }

                ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http fastcgi header: \"%V: %V\"", &h->key, &h->value);
                if (u->buffer.pos < u->buffer.last) {
                    continue;
                }
                /* the end of the FastCGI record */
                break;
            }
            if (rc == NGX_HTTP_PARSE_HEADER_DONE) {//这个表示后面啥东西都没有了。只有请求体的body数据了。
                /* a whole header has been parsed successfully */
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http fastcgi header done");
                if (u->headers_in.status) {//查看请求的状态码
                    status_line = &u->headers_in.status->value;
                    status = ngx_atoi(status_line->data, 3);
                    if (status == NGX_ERROR) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent invalid status \"%V\"", status_line);
                        return NGX_HTTP_UPSTREAM_INVALID_HEADER;
                    }
                    u->headers_in.status_n = status;//记录一下状态码。
                    u->headers_in.status_line = *status_line;

                } else if (u->headers_in.location) {//此请求是302请求。
                    u->headers_in.status_n = 302;
                    ngx_str_set(&u->headers_in.status_line,"302 Moved Temporarily");
                } else {//其他的话就默认200了
                    u->headers_in.status_n = 200;
                    ngx_str_set(&u->headers_in.status_line, "200 OK");
                }

                if (u->state) {//顺便记录一下请求的状态。
                    u->state->status = u->headers_in.status_n;
                }
                break;
            }
            /* there was error while a header line parsing */
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent invalid header");
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }//解析HTTP 头部数据结束。
        if (last) {//刚才为了处理HTTP请求行，临时将last设置为请求的结束。后面说不定还有东西。比如body啥的。
        //而且只要一个数据处理完毕，BODY肯定已经读取在某个FCGI请求行里面了
            u->buffer.last = last;
        }
        f->length -= u->buffer.pos - start;
        if (f->length == 0) {
            if (f->padding) {
                f->state = ngx_http_fastcgi_st_padding;
            } else {
                f->state = ngx_http_fastcgi_st_version;
            }
        }
        if (rc == NGX_HTTP_PARSE_HEADER_DONE) {
            return NGX_OK;//结束了，解析头部全部完成。
        }
        if (rc == NGX_OK) {
            continue;
        }
        /* rc == NGX_AGAIN */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "upstream split a header line in FastCGI records");
		//下面是什么意思呢，是指这次的请求头部数据，跨越了2个不同的FCGI 请求结构。所以这里需要记住他们。
        if (f->split_parts == NULL) {
            f->split_parts = ngx_array_create(r->pool, 1, sizeof(ngx_http_fastcgi_split_part_t));
            if (f->split_parts == NULL) {
                return NGX_ERROR;
            }
        }
        part = ngx_array_push(f->split_parts);
        part->start = part_start;
        part->end = part_end;
        if (u->buffer.pos < u->buffer.last) {
            continue;
        }
        return NGX_AGAIN;
    }
}


static ngx_int_t
ngx_http_fastcgi_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf)
{//这个函数在ngx_http_fastcgi_handler里面设置为p->input_filter，在FCGI给nginx发送数据的时候调用，解析FCGI的数据。
//ngx_event_pipe_read_upstream调用这里，来把已经读取的数据进行FCGI协议解析。
//这个函数处理一块FCGI数据buf，外层会循环调用的。
    u_char                  *m, *msg;
    ngx_int_t                rc;
    ngx_buf_t               *b, **prev;
    ngx_chain_t             *cl;
    ngx_http_request_t      *r;
    ngx_http_fastcgi_ctx_t  *f;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }
    r = p->input_ctx;
	//得到这个请求的协议上下文，比如我们这个包是第一个预读的包，那么现在的pos肯定不为0，而是位于中间部分。
    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    b = NULL;
    prev = &buf->shadow;//当前这个buf
    f->pos = buf->pos;
    f->last = buf->last;

    for ( ;; ) {
		//小于ngx_http_fastcgi_st_data状态的比较好处理，读，解析吧。后面就只有data,padding 2个状态了。
        if (f->state < ngx_http_fastcgi_st_data) {//还不是在处理数据的过程中。前面还有协议头部
        //下面简单处理一下FCGI的头部，将信息赋值到f的type,length,padding成员上。
            rc = ngx_http_fastcgi_process_record(r, f);
            if (rc == NGX_AGAIN) {
                break;//没数据了，等待读取
            }
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
            if (f->type == NGX_HTTP_FASTCGI_STDOUT && f->length == 0) {//如果协议头表示是标准输出，并且长度为0，那就是说明没有内容
                f->state = ngx_http_fastcgi_st_version;//又从下一个包头开始，也就是版本号。
                p->upstream_done = 1;
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, p->log, 0, "http fastcgi closed stdout");
                continue;
            }

            if (f->type == NGX_HTTP_FASTCGI_END_REQUEST) {//FCGI发送了关闭连接的请求。
                f->state = ngx_http_fastcgi_st_version;
                p->upstream_done = 1;
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, p->log, 0, "http fastcgi sent end request");
                break;
            }
        }
	
        if (f->state == ngx_http_fastcgi_st_padding) {//下面是读取padding的阶段，
            if (f->pos + f->padding < f->last) {//而正好当前缓冲区后面有足够的padding长度，那就直接用它，然后标记到下一个状态，继续处理吧
                f->state = ngx_http_fastcgi_st_version;
                f->pos += f->padding;
                continue;
            }
            if (f->pos + f->padding == f->last) {//刚好结束，那就退出循环，完成一块数据的解析。
                f->state = ngx_http_fastcgi_st_version;
                break;
            }
            f->padding -= f->last - f->pos;
            break;
        }
//到这里，就只有读取数据部分了。
        /* f->state == ngx_http_fastcgi_st_data */
        if (f->type == NGX_HTTP_FASTCGI_STDERR) {//这是标准错误输出，nginx会怎么处理呢，打印一条日志就行了。
            if (f->length) {//代表数据长度
                if (f->pos == f->last) {//后面没东西了，还需要下次再读取一点数据才能继续了
                    break;
                }
                msg = f->pos;
                if (f->pos + f->length <= f->last) {//错误信息已经全部读取到了，
                    f->pos += f->length;
                    f->length = 0;
                    f->state = ngx_http_fastcgi_st_padding;//下一步去处理padding
                } else {
                    f->length -= f->last - f->pos;
                    f->pos = f->last;
                }
                for (m = f->pos - 1; msg < m; m--) {//从错误信息的后面往前面扫，直到找到一个部位\r,\n . 空格 的字符为止，也就是过滤后面的这些字符吧。
                    if (*m != LF && *m != CR && *m != '.' && *m != ' ') {
                        break;
                    }
                }//就用来打印个日志。没其他的。
                ngx_log_error(NGX_LOG_ERR, p->log, 0, "FastCGI sent in stderr: \"%*s\"", m + 1 - msg, msg);
                if (f->pos == f->last) {
                    break;
                }
            } else {
                f->state = ngx_http_fastcgi_st_version;
            }
            continue;
        }
		//到这里就是标准的输出啦，也就是网页内容。
        /* f->type == NGX_HTTP_FASTCGI_STDOUT */
        if (f->pos == f->last) {
            break;//正好没有数据，返回
        }
        if (p->free) {//从free空闲ngx_buf_t结构中取一个
            b = p->free->buf;
            p->free = p->free->next;
        } else {
            b = ngx_alloc_buf(p->pool);
            if (b == NULL) {
                return NGX_ERROR;
            }
        }
		//用这个新的缓存描述结构，指向buf这块内存里面的标准输出数据部分，注意这里并没有拷贝数据，而是用b指向了f->pos也就是buf的某个数据地方。
        ngx_memzero(b, sizeof(ngx_buf_t));
        b->pos = f->pos;//从pos到end
        b->start = buf->start;//b 跟buf共享一块客户端发送过来的数据。这就是shadow的地方， 类似影子?
        b->end = buf->end;
        b->tag = p->tag;
        b->temporary = 1;
        b->recycled = 1;//设置为需要回收的标志，这样在发送数据时，会考虑回收这块内存的。为什么要设置为1呢，那buffer在哪呢
	//在函数开始处，prev = &buf->shadow;下面就用buf->shadow指向了这块新分配的b描述结构，其实数据是分开的，只是2个描述结构指向同一个buffer
        *prev = b;//实际上，这里第一次是将&buf->shadow指向b，没什么用，因为没人指向&buf->shadow自己。而对于所有的shadow，我们可以通过p->in组成链表的。不断追加在后面
        prev = &b->shadow;//这里用最开始的buf，也就是客户端接收到数据的那块数据buf的shadow成员，形成一个链表，里面每个元素都是FCGI的一个包的data部分数据。
//下面将当前分析得到的FCGI数据data部分放入p->in的链表里面。
        cl = ngx_alloc_chain_link(p->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }
        cl->buf = b;
        cl->next = NULL;
        if (p->in) {
            *p->last_in = cl;
        } else {
            p->in = cl;
        }
        p->last_in = &cl->next;//记住最后一块

		//同样，拷贝一下数据块序号。不过这里注意，buf可能包含好几个FCGI协议数据块，
		//那就可能存在多个in里面的b->num等于一个相同的buf->num.不要认为是一一映射。
        /* STUB */ b->num = buf->num;
        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf #%d %p", b->num, b->pos);
        if (f->pos + f->length < f->last) {//如果数据足够长，那修改一下f->pos，和f->state从而进入下一个数据包的处理。数据已经放入了p->in了的。
            if (f->padding) {
                f->state = ngx_http_fastcgi_st_padding;
            } else {
                f->state = ngx_http_fastcgi_st_version;
            }
            f->pos += f->length;
            b->last = f->pos;
            continue;//接收这块数据，继续下一块
        }
        if (f->pos + f->length == f->last) {//正好等于。下面可能需要读取padding，否则进入下一个数据包处理。
            if (f->padding) {
                f->state = ngx_http_fastcgi_st_padding;
            } else {
                f->state = ngx_http_fastcgi_st_version;
            }
            b->last = f->last;
            break;
        }
		//到这里，表示当前读取到的数据还少了，不够一个完整包的，那就用完这一点，然后返回，
		//等待下次event_pipe的时候再次read_upstream来读取一些数据再处理了。
        f->length -= f->last - f->pos;
        b->last = f->last;
        break;
    }//for循环结束。这个循环结束的条件为当前的buf数据已经全部处理完毕。
	
    if (b) {//刚才已经解析到了数据部分。
        b->shadow = buf;//将最后一块数据的shadow指向这块用来存放读入的裸FCGI数据块。干嘛用的呢，只是指向一下吗? 
        //不是，这里的shadow成员正好用来存储: 我这个b所在的大buf 的指针，这样通过不断遍历p->in我是可以找出这些小data部分的b所在的大数据块的。
        //具体看ngx_event_pipe_drain_chains里面.
        b->last_shadow = 1;//标记这是最后一块。
        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf %p %z", b->pos, b->last - b->pos);
        return NGX_OK;
    }
    /* there is no data record in the buf, add it to free chain */
    if (ngx_event_pipe_add_free_buf(p, buf) != NGX_OK) {//将buf挂入free_raw_bufs头部或者第二个位置，如果第一个位置有数据的话。
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_process_record(ngx_http_request_t *r, ngx_http_fastcgi_ctx_t *f)
{//简单处理了一下FCGI的请求头部，没有解析头部的类型含义，以及其里面的协议
    u_char                     ch, *p;
    ngx_http_fastcgi_state_e   state;
    state = f->state;//继上一次的状态继续开始
    for (p = f->pos; p < f->last; p++) {
        ch = *p;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http fastcgi record byte: %02Xd", ch);
        switch (state) {
        case ngx_http_fastcgi_st_version:
            if (ch != 1) {//只支持1版本
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent unsupported FastCGI protocol version: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_type;//下一个要看类型了
            break;

        case ngx_http_fastcgi_st_type:
            switch (ch) {
            case NGX_HTTP_FASTCGI_STDOUT://输出，后面有数据的
            case NGX_HTTP_FASTCGI_STDERR://错误
            case NGX_HTTP_FASTCGI_END_REQUEST://结束请求，OK了
                 f->type = (ngx_uint_t) ch;
                 break;
            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"upstream sent invalid FastCGI record type: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_request_id_hi;//下一个就看ID的了
            break;
		//好吧，从下面就知道了，nginx偷懒了，只支持单个连接一个请求
        /* we support the single request per connection */
        case ngx_http_fastcgi_st_request_id_hi:
            if (ch != 0) {///反正请求ID为1，高位肯定为0
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent unexpected FastCGI request id high byte: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_request_id_lo;
            break;

        case ngx_http_fastcgi_st_request_id_lo:
            if (ch != 1) {//必须为1，也就是请求的ID总是为1
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"upstream sent unexpected FastCGI request id low byte: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_content_length_hi;
            break;

        case ngx_http_fastcgi_st_content_length_hi:
            f->length = ch << 8;//高位，数据长度
            state = ngx_http_fastcgi_st_content_length_lo;
            break;

        case ngx_http_fastcgi_st_content_length_lo:
            f->length |= (size_t) ch;//
            state = ngx_http_fastcgi_st_padding_length;
            break;

        case ngx_http_fastcgi_st_padding_length:
            f->padding = (size_t) ch;
            state = ngx_http_fastcgi_st_reserved;
            break;

        case ngx_http_fastcgi_st_reserved:
            state = ngx_http_fastcgi_st_data;
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http fastcgi record length: %z", f->length);
            f->pos = p + 1;
            f->state = state;//记录一下刚才的状态，便于下回继续
            return NGX_OK;

        /* suppress warning */
        case ngx_http_fastcgi_st_data:
        case ngx_http_fastcgi_st_padding:
            break;
        }
    }
    f->state = state;//记录一下刚才的状态，便于下回继续
    return NGX_AGAIN;
}


static void
ngx_http_fastcgi_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http fastcgi request");

    return;
}


static void
ngx_http_fastcgi_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http fastcgi request");

    return;
}


static ngx_int_t
ngx_http_fastcgi_add_variables(ngx_conf_t *cf)
{
   ngx_http_variable_t  *var, *v;

    for (v = ngx_http_fastcgi_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_fastcgi_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_fastcgi_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_fastcgi_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.ignore_headers = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.cache_use_stale = 0;
     *     conf->upstream.cache_methods = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.hide_headers_hash = { NULL, 0 };
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     *     conf->upstream.store_lengths = NULL;
     *     conf->upstream.store_values = NULL;
     *
     *     conf->index.len = { 0, NULL };
     */

    conf->upstream.store = NGX_CONF_UNSET;
    conf->upstream.store_access = NGX_CONF_UNSET_UINT;
    conf->upstream.buffering = NGX_CONF_UNSET;
    conf->upstream.ignore_client_abort = NGX_CONF_UNSET;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    conf->upstream.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream.pass_request_body = NGX_CONF_UNSET;

#if (NGX_HTTP_CACHE)
    conf->upstream.cache = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_min_uses = NGX_CONF_UNSET_UINT;
    conf->upstream.cache_bypass = NGX_CONF_UNSET_PTR;
    conf->upstream.no_cache = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_valid = NGX_CONF_UNSET_PTR;
#endif

    conf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
    conf->upstream.pass_headers = NGX_CONF_UNSET_PTR;

    conf->upstream.intercept_errors = NGX_CONF_UNSET;

    /* "fastcgi_cyclic_temp_file" is disabled */
    conf->upstream.cyclic_temp_file = 0;

    conf->catch_stderr = NGX_CONF_UNSET_PTR;

    return conf;
}


static char *
ngx_http_fastcgi_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_fastcgi_loc_conf_t *prev = parent;
    ngx_http_fastcgi_loc_conf_t *conf = child;

    u_char                       *p;
    size_t                        size;
    uintptr_t                    *code;
    ngx_uint_t                    i;
    ngx_array_t                   headers_names;
    ngx_keyval_t                 *src;
    ngx_hash_key_t               *hk;
    ngx_hash_init_t               hash;
    ngx_http_core_loc_conf_t     *clcf;
    ngx_http_script_compile_t     sc;
    ngx_http_script_copy_code_t  *copy;

    if (conf->upstream.store != 0) {
        ngx_conf_merge_value(conf->upstream.store,
                              prev->upstream.store, 0);

        if (conf->upstream.store_lengths == NULL) {
            conf->upstream.store_lengths = prev->upstream.store_lengths;
            conf->upstream.store_values = prev->upstream.store_values;
        }
    }

    ngx_conf_merge_uint_value(conf->upstream.store_access,
                              prev->upstream.store_access, 0600);

    ngx_conf_merge_value(conf->upstream.buffering,
                              prev->upstream.buffering, 1);

    ngx_conf_merge_value(conf->upstream.ignore_client_abort,
                              prev->upstream.ignore_client_abort, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.send_lowat,
                              prev->upstream.send_lowat, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);


    ngx_conf_merge_bufs_value(conf->upstream.bufs, prev->upstream.bufs,
                              8, ngx_pagesize);

    if (conf->upstream.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"fastcgi_buffers\"");
        return NGX_CONF_ERROR;
    }


    size = conf->upstream.buffer_size;
    if (size < conf->upstream.bufs.size) {
        size = conf->upstream.bufs.size;
    }


    ngx_conf_merge_size_value(conf->upstream.busy_buffers_size_conf,
                              prev->upstream.busy_buffers_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.busy_buffers_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.busy_buffers_size = 2 * size;
    } else {
        conf->upstream.busy_buffers_size = conf->upstream.busy_buffers_size_conf;
    }

    if (conf->upstream.busy_buffers_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_busy_buffers_size\" must be equal or bigger than "
             "maximum of the value of \"fastcgi_buffer_size\" and "
             "one of the \"fastcgi_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream.busy_buffers_size > (conf->upstream.bufs.num - 1) * conf->upstream.bufs.size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_busy_buffers_size\" must be less than "
             "the size of all \"fastcgi_buffers\" minus one buffer");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.temp_file_write_size_conf,
                              prev->upstream.temp_file_write_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.temp_file_write_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.temp_file_write_size = 2 * size;
    } else {
        conf->upstream.temp_file_write_size =
                                      conf->upstream.temp_file_write_size_conf;
    }

    if (conf->upstream.temp_file_write_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_temp_file_write_size\" must be equal or bigger than "
             "maximum of the value of \"fastcgi_buffer_size\" and "
             "one of the \"fastcgi_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.max_temp_file_size_conf,
                              prev->upstream.max_temp_file_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.max_temp_file_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.max_temp_file_size = 1024 * 1024 * 1024;
    } else {
        conf->upstream.max_temp_file_size =
                                        conf->upstream.max_temp_file_size_conf;
    }

    if (conf->upstream.max_temp_file_size != 0
        && conf->upstream.max_temp_file_size < size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_max_temp_file_size\" must be equal to zero to disable "
             "the temporary files usage or must be equal or bigger than "
             "maximum of the value of \"fastcgi_buffer_size\" and "
             "one of the \"fastcgi_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_bitmask_value(conf->upstream.ignore_headers,
                              prev->upstream.ignore_headers,
                              NGX_CONF_BITMASK_SET);


    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (ngx_conf_merge_path_value(cf, &conf->upstream.temp_path,
                              prev->upstream.temp_path,
                              &ngx_http_fastcgi_temp_path)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

#if (NGX_HTTP_CACHE)

    ngx_conf_merge_ptr_value(conf->upstream.cache,
                              prev->upstream.cache, NULL);

    if (conf->upstream.cache && conf->upstream.cache->data == NULL) {
        ngx_shm_zone_t  *shm_zone;

        shm_zone = conf->upstream.cache;

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"fastcgi_cache\" zone \"%V\" is unknown",
                           &shm_zone->shm.name);

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_uint_value(conf->upstream.cache_min_uses,
                              prev->upstream.cache_min_uses, 1);

    ngx_conf_merge_bitmask_value(conf->upstream.cache_use_stale,
                              prev->upstream.cache_use_stale,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_OFF));

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.cache_use_stale = NGX_CONF_BITMASK_SET
                                         |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.cache_methods == 0) {
        conf->upstream.cache_methods = prev->upstream.cache_methods;
    }

    conf->upstream.cache_methods |= NGX_HTTP_GET|NGX_HTTP_HEAD;

    ngx_conf_merge_ptr_value(conf->upstream.cache_bypass,
                             prev->upstream.cache_bypass, NULL);

    ngx_conf_merge_ptr_value(conf->upstream.no_cache,
                             prev->upstream.no_cache, NULL);

    if (conf->upstream.no_cache && conf->upstream.cache_bypass == NULL) {
        ngx_log_error(NGX_LOG_WARN, cf->log, 0,
             "\"fastcgi_no_cache\" functionality has been changed in 0.8.46, "
             "now it should be used together with \"fastcgi_cache_bypass\"");
    }

    ngx_conf_merge_ptr_value(conf->upstream.cache_valid,
                             prev->upstream.cache_valid, NULL);

    if (conf->cache_key.value.data == NULL) {
        conf->cache_key = prev->cache_key;
    }

#endif

    ngx_conf_merge_value(conf->upstream.pass_request_headers,
                              prev->upstream.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream.pass_request_body,
                              prev->upstream.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream.intercept_errors,
                              prev->upstream.intercept_errors, 0);

    ngx_conf_merge_ptr_value(conf->catch_stderr, prev->catch_stderr, NULL);


    ngx_conf_merge_str_value(conf->index, prev->index, "");

    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "fastcgi_hide_headers_hash";

    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream,
             &prev->upstream, ngx_http_fastcgi_hide_headers, &hash)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    if (conf->fastcgi_lengths == NULL) {
        conf->fastcgi_lengths = prev->fastcgi_lengths;
        conf->fastcgi_values = prev->fastcgi_values;
    }

    if (conf->upstream.upstream || conf->fastcgi_lengths) {
        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        if (clcf->handler == NULL && clcf->lmt_excpt) {
            clcf->handler = ngx_http_fastcgi_handler;
        }
    }

#if (NGX_PCRE)
    if (conf->split_regex == NULL) {
        conf->split_regex = prev->split_regex;
        conf->split_name = prev->split_name;
    }
#endif

    if (conf->params_source == NULL) {
        conf->flushes = prev->flushes;
        conf->params_len = prev->params_len;
        conf->params = prev->params;
        conf->params_source = prev->params_source;
        conf->headers_hash = prev->headers_hash;

#if (NGX_HTTP_CACHE)

        if (conf->params_source == NULL) {

            if ((conf->upstream.cache == NULL)
                == (prev->upstream.cache == NULL))
            {
                return NGX_CONF_OK;
            }

            /* 6 is a number of ngx_http_fastcgi_cache_headers entries */
            conf->params_source = ngx_array_create(cf->pool, 6,
                                                   sizeof(ngx_keyval_t));
            if (conf->params_source == NULL) {
                return NGX_CONF_ERROR;
            }
        }
#else

        if (conf->params_source == NULL) {
            return NGX_CONF_OK;
        }

#endif
    }

    conf->params_len = ngx_array_create(cf->pool, 64, 1);
    if (conf->params_len == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->params = ngx_array_create(cf->pool, 512, 1);
    if (conf->params == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_array_init(&headers_names, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    src = conf->params_source->elts;

#if (NGX_HTTP_CACHE)

    if (conf->upstream.cache) {
        ngx_keyval_t  *h, *s;

        for (h = ngx_http_fastcgi_cache_headers; h->key.len; h++) {

            for (i = 0; i < conf->params_source->nelts; i++) {
                if (ngx_strcasecmp(h->key.data, src[i].key.data) == 0) {
                    goto next;
                }
            }

            s = ngx_array_push(conf->params_source);
            if (s == NULL) {
                return NGX_CONF_ERROR;
            }

            *s = *h;

            src = conf->params_source->elts;

        next:

            h++;
        }
    }

#endif

    for (i = 0; i < conf->params_source->nelts; i++) {

        if (src[i].key.len > sizeof("HTTP_") - 1
            && ngx_strncmp(src[i].key.data, "HTTP_", sizeof("HTTP_") - 1) == 0)
        {
            hk = ngx_array_push(&headers_names);
            if (hk == NULL) {
                return NGX_CONF_ERROR;
            }

            hk->key.len = src[i].key.len - 5;
            hk->key.data = src[i].key.data + 5;
            hk->key_hash = ngx_hash_key_lc(hk->key.data, hk->key.len);
            hk->value = (void *) 1;

            if (src[i].value.len == 0) {
                continue;
            }
        }

        copy = ngx_array_push_n(conf->params_len,
                                sizeof(ngx_http_script_copy_code_t));
        if (copy == NULL) {
            return NGX_CONF_ERROR;
        }

        copy->code = (ngx_http_script_code_pt) ngx_http_script_copy_len_code;
        copy->len = src[i].key.len;


        size = (sizeof(ngx_http_script_copy_code_t)
                + src[i].key.len + sizeof(uintptr_t) - 1)
                & ~(sizeof(uintptr_t) - 1);

        copy = ngx_array_push_n(conf->params, size);
        if (copy == NULL) {
            return NGX_CONF_ERROR;
        }

        copy->code = ngx_http_script_copy_code;
        copy->len = src[i].key.len;

        p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
        ngx_memcpy(p, src[i].key.data, src[i].key.len);


        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = &src[i].value;
        sc.flushes = &conf->flushes;
        sc.lengths = &conf->params_len;
        sc.values = &conf->params;

        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        code = ngx_array_push_n(conf->params_len, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_CONF_ERROR;
        }

        *code = (uintptr_t) NULL;


        code = ngx_array_push_n(conf->params, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_CONF_ERROR;
        }

        *code = (uintptr_t) NULL;
    }

    code = ngx_array_push_n(conf->params_len, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_CONF_ERROR;
    }

    *code = (uintptr_t) NULL;


    conf->header_params = headers_names.nelts;

    hash.hash = &conf->headers_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = 64;
    hash.name = "fastcgi_params_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, headers_names.elts, headers_names.nelts) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_fastcgi_script_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                       *p;
    ngx_http_fastcgi_ctx_t       *f;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    f = ngx_http_fastcgi_split(r, flcf);

    if (f == NULL) {
        return NGX_ERROR;
    }

    if (f->script_name.len == 0
        || f->script_name.data[f->script_name.len - 1] != '/')
    {
        v->len = f->script_name.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = f->script_name.data;

        return NGX_OK;
    }

    v->len = f->script_name.len + flcf->index.len;

    v->data = ngx_pnalloc(r->pool, v->len);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    p = ngx_copy(v->data, f->script_name.data, f->script_name.len);
    ngx_memcpy(p, flcf->index.data, flcf->index.len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_path_info_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_fastcgi_ctx_t       *f;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    f = ngx_http_fastcgi_split(r, flcf);

    if (f == NULL) {
        return NGX_ERROR;
    }

    v->len = f->path_info.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = f->path_info.data;

    return NGX_OK;
}


static ngx_http_fastcgi_ctx_t *
ngx_http_fastcgi_split(ngx_http_request_t *r, ngx_http_fastcgi_loc_conf_t *flcf)
{
    ngx_http_fastcgi_ctx_t       *f;
#if (NGX_PCRE)
    ngx_int_t                     n;
    int                           captures[(1 + 2) * 3];

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (f == NULL) {
        f = ngx_pcalloc(r->pool, sizeof(ngx_http_fastcgi_ctx_t));
        if (f == NULL) {
            return NULL;
        }

        ngx_http_set_ctx(r, f, ngx_http_fastcgi_module);
    }

    if (f->script_name.len) {
        return f;
    }

    if (flcf->split_regex == NULL) {
        f->script_name = r->uri;
        return f;
    }

    n = ngx_regex_exec(flcf->split_regex, &r->uri, captures, (1 + 2) * 3);

    if (n >= 0) { /* match */
        f->script_name.len = captures[3] - captures[2];
        f->script_name.data = r->uri.data + captures[2];

        f->path_info.len = captures[5] - captures[4];
        f->path_info.data = r->uri.data + captures[4];

        return f;
    }

    if (n == NGX_REGEX_NO_MATCHED) {
        f->script_name = r->uri;
        return f;
    }

    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                  ngx_regex_exec_n " failed: %i on \"%V\" using \"%V\"",
                  n, &r->uri, &flcf->split_name);
    return NULL;

#else

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (f == NULL) {
        f = ngx_pcalloc(r->pool, sizeof(ngx_http_fastcgi_ctx_t));
        if (f == NULL) {
            return NULL;
        }

        ngx_http_set_ctx(r, f, ngx_http_fastcgi_module);
    }

    f->script_name = r->uri;

    return f;

#endif
}


static char *
ngx_http_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//nginx 解析到fastcgi_pass指令的时候调用这里.比如fastcgi_pass   127.0.0.1:8777;
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_url_t                   u;
    ngx_str_t                  *value, *url;
    ngx_uint_t                  n;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_script_compile_t   sc;

    if (flcf->upstream.upstream || flcf->fastcgi_lengths) {
        return "is duplicate";
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	//设置句柄，会在ngx_http_update_location_config里面设置为content_handle的，从而在content phase中被调用
    clcf->handler = ngx_http_fastcgi_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    value = cf->args->elts;
    url = &value[1];//得到127.0.0.1:8777
    n = ngx_http_script_variables_count(url);//以'$'开头的变量有多少

    if (n) {//进行变量解析
        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
        sc.cf = cf;
        sc.source = url;
        sc.lengths = &flcf->fastcgi_lengths;
        sc.values = &flcf->fastcgi_values;
        sc.variables = n;
        sc.complete_lengths = 1;
        sc.complete_values = 1;//启用脚本引擎进行简析，其简析的code等放入fastcgi_lengths和fastcgi_values
        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = value[1];
    u.no_resolve = 1;
	//当做单个server的upstream加入到upstream里面
    flcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (flcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_fastcgi_split_path_info(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_PCRE)
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t            *value;
    ngx_regex_compile_t   rc;
    u_char                errstr[NGX_MAX_CONF_ERRSTR];

    value = cf->args->elts;

    flcf->split_name = value[1];

    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    rc.pattern = value[1];
    rc.pool = cf->pool;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    if (ngx_regex_compile(&rc) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V", &rc.err);
        return NGX_CONF_ERROR;
    }

    if (rc.captures != 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "pattern \"%V\" must have 2 captures", &value[1]);
        return NGX_CONF_ERROR;
    }

    flcf->split_regex = rc.regex;

    return NGX_CONF_OK;

#else

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "\"%V\" requires PCRE library", &cmd->name);
    return NGX_CONF_ERROR;

#endif
}


static char *
ngx_http_fastcgi_store(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t                  *value;
    ngx_http_script_compile_t   sc;

    if (flcf->upstream.store != NGX_CONF_UNSET
        || flcf->upstream.store_lengths)
    {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        flcf->upstream.store = 0;
        return NGX_CONF_OK;
    }

#if (NGX_HTTP_CACHE)

    if (flcf->upstream.cache != NGX_CONF_UNSET_PTR
        && flcf->upstream.cache != NULL)
    {
        return "is incompatible with \"fastcgi_cache\"";
    }

#endif

    if (ngx_strcmp(value[1].data, "on") == 0) {
        flcf->upstream.store = 1;
        return NGX_CONF_OK;
    }

    /* include the terminating '\0' into script */
    value[1].len++;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = &value[1];
    sc.lengths = &flcf->upstream.store_lengths;
    sc.values = &flcf->upstream.store_values;
    sc.variables = ngx_http_script_variables_count(&value[1]);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


#if (NGX_HTTP_CACHE)

static char *
ngx_http_fastcgi_cache(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t  *value;

    value = cf->args->elts;

    if (flcf->upstream.cache != NGX_CONF_UNSET_PTR) {
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        flcf->upstream.cache = NULL;
        return NGX_CONF_OK;
    }

    if (flcf->upstream.store > 0 || flcf->upstream.store_lengths) {
        return "is incompatible with \"fastcgi_store\"";
    }

    flcf->upstream.cache = ngx_shared_memory_add(cf, &value[1], 0,
                                                 &ngx_http_fastcgi_module);
    if (flcf->upstream.cache == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_fastcgi_cache_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (flcf->cache_key.value.len) {
        return "is duplicate";
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &flcf->cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


static char *
ngx_http_fastcgi_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;

    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"fastcgi_send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"fastcgi_send_lowat\" is not supported, ignored");

    *np = 0;

#endif

    return NGX_CONF_OK;
}
