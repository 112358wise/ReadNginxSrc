
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    u_char    *name;
    uint32_t   method;
} ngx_http_method_name_t;


#define NGX_HTTP_REQUEST_BODY_FILE_OFF    0
#define NGX_HTTP_REQUEST_BODY_FILE_ON     1
#define NGX_HTTP_REQUEST_BODY_FILE_CLEAN  2


static ngx_int_t ngx_http_core_find_location(ngx_http_request_t *r);
static ngx_int_t ngx_http_core_find_static_location(ngx_http_request_t *r,
    ngx_http_location_tree_node_t *node);

static ngx_int_t ngx_http_core_preconfiguration(ngx_conf_t *cf);
static void *ngx_http_core_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_core_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_core_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_core_merge_srv_conf(ngx_conf_t *cf,
    void *parent, void *child);
static void *ngx_http_core_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_core_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_core_server(ngx_conf_t *cf, ngx_command_t *cmd,
    void *dummy);
static char *ngx_http_core_location(ngx_conf_t *cf, ngx_command_t *cmd,
    void *dummy);
static ngx_int_t ngx_http_core_regex_location(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *clcf, ngx_str_t *regex, ngx_uint_t caseless);

static char *ngx_http_core_types(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_type(ngx_conf_t *cf, ngx_command_t *dummy,
    void *conf);

static char *ngx_http_core_listen(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_server_name(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_root(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_core_limit_except(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_directio(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_error_page(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_try_files(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_open_file_cache(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_error_log(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_keepalive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_internal(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_core_resolver(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#if (NGX_HTTP_GZIP)
static char *ngx_http_gzip_disable(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#endif

static char *ngx_http_core_lowat_check(ngx_conf_t *cf, void *post, void *data);
static char *ngx_http_core_pool_size(ngx_conf_t *cf, void *post, void *data);

static ngx_conf_post_t  ngx_http_core_lowat_post =
    { ngx_http_core_lowat_check };

static ngx_conf_post_handler_pt  ngx_http_core_pool_size_p =
    ngx_http_core_pool_size;

static ngx_conf_deprecated_t  ngx_conf_deprecated_optimize_server_names = {
    ngx_conf_deprecated, "optimize_server_names", "server_name_in_redirect"
};

static ngx_conf_deprecated_t  ngx_conf_deprecated_open_file_cache_retest = {
    ngx_conf_deprecated, "open_file_cache_retest", "open_file_cache_valid"
};

static ngx_conf_deprecated_t  ngx_conf_deprecated_satisfy_any = {
    ngx_conf_deprecated, "satisfy_any", "satisfy"
};


static ngx_conf_enum_t  ngx_http_core_request_body_in_file[] = {
    { ngx_string("off"), NGX_HTTP_REQUEST_BODY_FILE_OFF },
    { ngx_string("on"), NGX_HTTP_REQUEST_BODY_FILE_ON },
    { ngx_string("clean"), NGX_HTTP_REQUEST_BODY_FILE_CLEAN },
    { ngx_null_string, 0 }
};


#if (NGX_HAVE_FILE_AIO)

static ngx_conf_enum_t  ngx_http_core_aio[] = {
    { ngx_string("off"), NGX_HTTP_AIO_OFF  },
    { ngx_string("on"), NGX_HTTP_AIO_ON },
#if (NGX_HAVE_AIO_SENDFILE)
    { ngx_string("sendfile"), NGX_HTTP_AIO_SENDFILE },
#endif
    { ngx_null_string, 0 }
};

#endif


static ngx_conf_enum_t  ngx_http_core_satisfy[] = {
    { ngx_string("all"), NGX_HTTP_SATISFY_ALL },
    { ngx_string("any"), NGX_HTTP_SATISFY_ANY },
    { ngx_null_string, 0 }
};


static ngx_conf_enum_t  ngx_http_core_if_modified_since[] = {
    { ngx_string("off"), NGX_HTTP_IMS_OFF },
    { ngx_string("exact"), NGX_HTTP_IMS_EXACT },
    { ngx_string("before"), NGX_HTTP_IMS_BEFORE },
    { ngx_null_string, 0 }
};


static ngx_path_init_t  ngx_http_client_temp_path = {
    ngx_string(NGX_HTTP_CLIENT_TEMP_PATH), { 0, 0, 0 }
};


#if (NGX_HTTP_GZIP)

static ngx_conf_enum_t  ngx_http_gzip_http_version[] = {
    { ngx_string("1.0"), NGX_HTTP_VERSION_10 },
    { ngx_string("1.1"), NGX_HTTP_VERSION_11 },
    { ngx_null_string, 0 }
};


static ngx_conf_bitmask_t  ngx_http_gzip_proxied_mask[] = {
    { ngx_string("off"), NGX_HTTP_GZIP_PROXIED_OFF },
    { ngx_string("expired"), NGX_HTTP_GZIP_PROXIED_EXPIRED },
    { ngx_string("no-cache"), NGX_HTTP_GZIP_PROXIED_NO_CACHE },
    { ngx_string("no-store"), NGX_HTTP_GZIP_PROXIED_NO_STORE },
    { ngx_string("private"), NGX_HTTP_GZIP_PROXIED_PRIVATE },
    { ngx_string("no_last_modified"), NGX_HTTP_GZIP_PROXIED_NO_LM },
    { ngx_string("no_etag"), NGX_HTTP_GZIP_PROXIED_NO_ETAG },
    { ngx_string("auth"), NGX_HTTP_GZIP_PROXIED_AUTH },
    { ngx_string("any"), NGX_HTTP_GZIP_PROXIED_ANY },
    { ngx_null_string, 0 }
};


static ngx_str_t  ngx_http_gzip_no_cache = ngx_string("no-cache");
static ngx_str_t  ngx_http_gzip_no_store = ngx_string("no-store");
static ngx_str_t  ngx_http_gzip_private = ngx_string("private");

#endif


static ngx_command_t  ngx_http_core_commands[] = {

    { ngx_string("variables_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_core_main_conf_t, variables_hash_max_size),
      NULL },

    { ngx_string("variables_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_core_main_conf_t, variables_hash_bucket_size),
      NULL },

    { ngx_string("server_names_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_core_main_conf_t, server_names_hash_max_size),
      NULL },

    { ngx_string("server_names_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_core_main_conf_t, server_names_hash_bucket_size),
      NULL },

    { ngx_string("server"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_MULTI|NGX_CONF_NOARGS,
      ngx_http_core_server,
      0,
      0,
      NULL },

    { ngx_string("connection_pool_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, connection_pool_size),
      &ngx_http_core_pool_size_p },

    { ngx_string("request_pool_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, request_pool_size),
      &ngx_http_core_pool_size_p },

    { ngx_string("client_header_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, client_header_timeout),
      NULL },

    { ngx_string("client_header_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, client_header_buffer_size),
      NULL },

    { ngx_string("large_client_header_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, large_client_header_buffers),
      NULL },

    { ngx_string("optimize_server_names"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, server_name_in_redirect),
      &ngx_conf_deprecated_optimize_server_names },

    { ngx_string("ignore_invalid_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, ignore_invalid_headers),
      NULL },

    { ngx_string("merge_slashes"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, merge_slashes),
      NULL },

    { ngx_string("underscores_in_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_core_srv_conf_t, underscores_in_headers),
      NULL },

    { ngx_string("location"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE12,
      ngx_http_core_location,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("listen"),
      NGX_HTTP_SRV_CONF|NGX_CONF_1MORE,
      ngx_http_core_listen,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("server_name"),
      NGX_HTTP_SRV_CONF|NGX_CONF_1MORE,
      ngx_http_core_server_name,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("types_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, types_hash_max_size),
      NULL },

    { ngx_string("types_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, types_hash_bucket_size),
      NULL },

    { ngx_string("types"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
                                          |NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_core_types,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("default_type"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, default_type),
      NULL },

    { ngx_string("root"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_core_root,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("alias"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_core_root,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("limit_except"),
      NGX_HTTP_LOC_CONF|NGX_CONF_BLOCK|NGX_CONF_1MORE,
      ngx_http_core_limit_except,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("client_max_body_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_off_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, client_max_body_size),
      NULL },

    { ngx_string("client_body_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, client_body_buffer_size),
      NULL },

    { ngx_string("client_body_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, client_body_timeout),
      NULL },

    { ngx_string("client_body_temp_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
      ngx_conf_set_path_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, client_body_temp_path),
      NULL },

    { ngx_string("client_body_in_file_only"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, client_body_in_file_only),
      &ngx_http_core_request_body_in_file },

    { ngx_string("client_body_in_single_buffer"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, client_body_in_single_buffer),
      NULL },

    { ngx_string("sendfile"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, sendfile),
      NULL },

    { ngx_string("sendfile_max_chunk"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, sendfile_max_chunk),
      NULL },

#if (NGX_HAVE_FILE_AIO)

    { ngx_string("aio"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, aio),
      &ngx_http_core_aio },

#endif

    { ngx_string("read_ahead"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, read_ahead),
      NULL },

    { ngx_string("directio"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_core_directio,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("directio_alignment"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_off_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, directio_alignment),
      NULL },

    { ngx_string("tcp_nopush"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, tcp_nopush),
      NULL },

    { ngx_string("tcp_nodelay"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, tcp_nodelay),
      NULL },

    { ngx_string("send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, send_timeout),
      NULL },

    { ngx_string("send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, send_lowat),
      &ngx_http_core_lowat_post },

    { ngx_string("postpone_output"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, postpone_output),
      NULL },

    { ngx_string("limit_rate"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, limit_rate),
      NULL },

    { ngx_string("limit_rate_after"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, limit_rate_after),
      NULL },

    { ngx_string("keepalive_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_core_keepalive,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("keepalive_requests"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, keepalive_requests),
      NULL },

    { ngx_string("satisfy"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, satisfy),
      &ngx_http_core_satisfy },

    { ngx_string("satisfy_any"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, satisfy),
      &ngx_conf_deprecated_satisfy_any },

    { ngx_string("internal"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_core_internal,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lingering_time"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, lingering_time),
      NULL },

    { ngx_string("lingering_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, lingering_timeout),
      NULL },

    { ngx_string("reset_timedout_connection"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, reset_timedout_connection),
      NULL },

    { ngx_string("server_name_in_redirect"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, server_name_in_redirect),
      NULL },

    { ngx_string("port_in_redirect"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, port_in_redirect),
      NULL },

    { ngx_string("msie_padding"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, msie_padding),
      NULL },

    { ngx_string("msie_refresh"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, msie_refresh),
      NULL },

    { ngx_string("log_not_found"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, log_not_found),
      NULL },

    { ngx_string("log_subrequest"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, log_subrequest),
      NULL },

    { ngx_string("recursive_error_pages"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, recursive_error_pages),
      NULL },

    { ngx_string("server_tokens"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, server_tokens),
      NULL },

    { ngx_string("if_modified_since"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, if_modified_since),
      &ngx_http_core_if_modified_since },

    { ngx_string("chunked_transfer_encoding"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, chunked_transfer_encoding),
      NULL },

    { ngx_string("error_page"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_2MORE,
      ngx_http_core_error_page,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("try_files"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_2MORE,
      ngx_http_core_try_files,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("post_action"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, post_action),
      NULL },

    { ngx_string("error_log"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_core_error_log,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("open_file_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_core_open_file_cache,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, open_file_cache),
      NULL },

    { ngx_string("open_file_cache_valid"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_sec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, open_file_cache_valid),
      NULL },

    { ngx_string("open_file_cache_retest"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_sec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, open_file_cache_valid),
      &ngx_conf_deprecated_open_file_cache_retest },

    { ngx_string("open_file_cache_min_uses"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, open_file_cache_min_uses),
      NULL },

    { ngx_string("open_file_cache_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, open_file_cache_errors),
      NULL },

    { ngx_string("open_file_cache_events"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, open_file_cache_events),
      NULL },

    { ngx_string("resolver"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_core_resolver,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("resolver_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, resolver_timeout),
      NULL },

#if (NGX_HTTP_GZIP)

    { ngx_string("gzip_vary"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, gzip_vary),
      NULL },

    { ngx_string("gzip_http_version"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, gzip_http_version),
      &ngx_http_gzip_http_version },

    { ngx_string("gzip_proxied"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_core_loc_conf_t, gzip_proxied),
      &ngx_http_gzip_proxied_mask },

    { ngx_string("gzip_disable"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_gzip_disable,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

#endif

      ngx_null_command
};


static ngx_http_module_t  ngx_http_core_module_ctx = {
    ngx_http_core_preconfiguration,        /* preconfiguration */
    NULL,                                  /* postconfiguration */
    ngx_http_core_create_main_conf,        /* create main configuration */
    ngx_http_core_init_main_conf,          /* init main configuration */

    ngx_http_core_create_srv_conf,         /* create server configuration */
    ngx_http_core_merge_srv_conf,          /* merge server configuration */

    ngx_http_core_create_loc_conf,         /* create location configuration */
    ngx_http_core_merge_loc_conf           /* merge location configuration */
};


ngx_module_t  ngx_http_core_module = {
    NGX_MODULE_V1,
    &ngx_http_core_module_ctx,             /* module context */
    ngx_http_core_commands,                /* module directives */
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


ngx_str_t  ngx_http_core_get_method = { 3, (u_char *) "GET " };


void ngx_http_handler(ngx_http_request_t *r)
{//ngx_http_process_request调用这里，进入处理过程的解析，重定向等。此时HEADER已经读取完毕。
//	ngx_http_process_request和ngx_http_internal_redirect调用这里。

    ngx_http_core_main_conf_t  *cmcf;
    r->connection->log->action = NULL;
    r->connection->unexpected_eof = 0;
    if (!r->internal) {//刚开始internal为0，表示不是在内部重定向过程中。
        switch (r->headers_in.connection_type) {////连接是keep-alive还是要close
        case 0://没有设置。则进行默认。需要看是否是HTTP 1.1。 HTTP 1.0不支持keepalive
            if (r->http_version > NGX_HTTP_VERSION_10) {
                r->keepalive = 1;
            } else {
                r->keepalive = 0;
            }
            break;
        case NGX_HTTP_CONNECTION_CLOSE:
            r->keepalive = 0;
            break;
        case NGX_HTTP_CONNECTION_KEEP_ALIVE:
            r->keepalive = 1;
            break;
        }
        if (r->keepalive) {//如果要保持连接。如下几种情况不支持，比如IE6的POST，safari浏览器。
            if (r->headers_in.msie6) {
                if (r->method == NGX_HTTP_POST) {
                    /*
                     * MSIE may wait for some time if an response for
                     * a POST request was sent over a keepalive connection
                     */
                    r->keepalive = 0;
                }
            } else if (r->headers_in.safari) {
                /*
                 * Safari may send a POST request to a closed keepalive
                 * connection and stalls for some time
                 */
                r->keepalive = 0;
            }
        }
        if (r->headers_in.content_length_n > 0) {
            r->lingering_close = 1;
//lingering_close用于在请求完成后，接收客户端发送的额外数据并全部忽略掉，防止因关闭连接时接收缓冲区中仍有数据而停止发送响应并返回RST。
        } else {
            r->lingering_close = 0;
        }
        r->phase_handler = 0;//因为不是在内部重定向循环里面，因此肯定是第一次进入，那就从第一个阶段开始处理吧。
    } else {
    //为1表示:设置请求为内部重定向状态。通知ngx_http_handler,进行间断选择的时候从server_rewrite_index开始进行循环处理，不然又回去了
        cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
        r->phase_handler = cmcf->phase_engine.server_rewrite_index;//获取rewrite过程的下标，从那里开始解析。
    }
    r->valid_location = 1;
#if (NGX_HTTP_GZIP)
    r->gzip_tested = 0;
    r->gzip_ok = 0;
    r->gzip_vary = 0;
#endif
    r->write_event_handler = ngx_http_core_run_phases;//请求的可写事件处理设置为这个，这样就调起了过程处理。
    ngx_http_core_run_phases(r);
}

/* 上面的各个阶段默认情况下的句柄分别为:ngx_http_init_phase_handlers里面设置的
0 {checker = 0x42f71e <ngx_http_core_generic_phase>, handler = 0x45963a <ngx_http_realip_handler>, next = 1}
1 {checker = 0x42f792 <ngx_http_core_rewrite_phase>, handler = 0x45d41e <ngx_http_rewrite_handler>, next = 2}
2 {checker = 0x42fc03 <ngx_http_core_find_config_phase>, handler = 0, next = 0}
3 {checker = 0x42f792 <ngx_http_core_rewrite_phase>, handler = 0x45d41e <ngx_http_rewrite_handler>, next = 4}
4 {checker = 0x42f7cd <ngx_http_core_post_rewrite_phase>, handler = 0, next = 2}
5 {checker = 0x42f71e <ngx_http_core_generic_phase>, handler = 0x45963a <ngx_http_realip_handler>, next = 8}
6 {checker = 0x42f71e <ngx_http_core_generic_phase>, handler = 0x458b17 <ngx_http_limit_req_handler>, next = 8}
7 {checker = 0x42f71e <ngx_http_core_generic_phase>, handler = 0x457c9a <ngx_http_limit_conn_handler>, next = 8}
8 {checker = 0x42f889 <ngx_http_core_access_phase>, handler = 0x45711a <ngx_http_access_handler>, next = 11}//简单的IP禁止
9 {checker = 0x42f889 <ngx_http_core_access_phase>, handler = 0x456b81 <ngx_http_auth_basic_handler>, next = 11}//简单HTTP密码验证
10 {checker = 0x42f9a0 <ngx_http_core_post_access_phase>, handler = 0, next = 11}
11 {checker = 0x4305a8 <ngx_http_core_content_phase>, handler = 0x449afb <ngx_http_index_handler>, next = 14}
12 {checker = 0x4305a8 <ngx_http_core_content_phase>, handler = 0x455a22 <ngx_http_autoindex_handler>, next = 14}
13 {checker = 0x4305a8 <ngx_http_core_content_phase>, handler = 0x449241 <ngx_http_static_handler>, next = 14}
14 {checker = 0, handler = 0x8d200002, next = 0}

*/
void ngx_http_core_run_phases(ngx_http_request_t *r)
{
    ngx_int_t                   rc;
    ngx_http_phase_handler_t   *ph;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    ph = cmcf->phase_engine.handlers;
    while (ph[r->phase_handler].checker) {//一个个过程往后面进行处理
        rc = ph[r->phase_handler].checker(r, &ph[r->phase_handler]);//里面会改变r->phase_handler的。也就是id会变化，增加
        if (rc == NGX_OK) {//如果返回OK，则说明请求完毕，返回
            return;
        }
    }
}


ngx_int_t
ngx_http_core_generic_phase(ngx_http_request_t *r, ngx_http_phase_handler_t *ph)
{//第一步和最后一步调用。见上面的数组列表。
    ngx_int_t  rc;
    /*
     * generic phase checker,
     * used by the post read and pre-access phases
     */
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "generic phase: %ui", r->phase_handler);
	//ngx_http_realip_handler 或者ngx_http_log_handler 
    rc = ph->handler(r);
    if (rc == NGX_OK) {
        r->phase_handler = ph->next;
        return NGX_AGAIN;
    }
    if (rc == NGX_DECLINED) {
        r->phase_handler++;
        return NGX_AGAIN;
    }
    if (rc == NGX_AGAIN || rc == NGX_DONE) {
        return NGX_OK;
    }
    /* rc == NGX_ERROR || rc == NGX_HTTP_...  */
    ngx_http_finalize_request(r, rc);
    return NGX_OK;
}


ngx_int_t
ngx_http_core_rewrite_phase(ngx_http_request_t *r, ngx_http_phase_handler_t *ph)
{
    ngx_int_t  rc;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "rewrite phase: %ui", r->phase_handler);
	//调用ngx_http_rewrite_handler
    rc = ph->handler(r);

    if (rc == NGX_DECLINED) {//后面的处理过程还要继续处理。
        r->phase_handler++;
        return NGX_AGAIN;
    }

    if (rc == NGX_DONE) {//处理完成，则将推出处理过程的解析。
        return NGX_OK;
    }

    /* NGX_OK, NGX_AGAIN, NGX_ERROR, NGX_HTTP_...  */
    ngx_http_finalize_request(r, rc);

    return NGX_OK;
}


ngx_int_t ngx_http_core_find_config_phase(ngx_http_request_t *r, ngx_http_phase_handler_t *ph)
{
    u_char                    *p;
    size_t                     len;
    ngx_int_t                  rc;
    ngx_http_core_loc_conf_t  *clcf;
    r->content_handler = NULL;//首先初始化content_handler，这个会在ngx_http_core_content_phase里面使用
    r->uri_changed = 0;
	/*
 * NGX_OK       - exact or regex match
 * NGX_DONE     - auto redirect
 * NGX_AGAIN    - inclusive match
 * NGX_ERROR    - regex error
 * NGX_DECLINED - no match
 */
    rc = ngx_http_core_find_location(r);//解析完HTTP{}块后，ngx_http_init_static_location_trees函数会创建一颗inclusive的三叉树，以加速配置查找。
	//找到所属的location，并且loc_conf也已经更新了r->loc_conf了。
    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_OK;
    }
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);//用刚找到的loc_conf，得到其http_core模块的位置配置。
    if (!r->internal && clcf->internal) {//是否是i在内部重定向，如果是，中断吗 �
        ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
        return NGX_OK;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP,r->connection->log,0,"using configuration \"%s%V\"",
		(clcf->noname ?"*":(clcf->exact_match?"=":"")),&clcf->name);
	//这个很重要，更新location配置，主要是 r->content_handler = clcf->handler;设置回调从而在content_phrase阶段用这个handler。
    ngx_http_update_location_config(r);
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,  "http cl:%O max:%O",
                   r->headers_in.content_length_n, clcf->client_max_body_size);

    if (r->headers_in.content_length_n != -1 && !r->discard_body //长度超长了。拒绝
        && clcf->client_max_body_size  && clcf->client_max_body_size < r->headers_in.content_length_n)  {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "client intended to send too large body: %O bytes", r->headers_in.content_length_n);
        (void) ngx_http_discard_request_body(r);
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_ENTITY_TOO_LARGE);
        return NGX_OK;
    }

    if (rc == NGX_DONE) {//auto redirect,需要进行重定向。下面就给客户端返回301，带上正确的location头部
        r->headers_out.location = ngx_list_push(&r->headers_out.headers);//增加一个location头
        if (r->headers_out.location == NULL) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_OK;
        }
        /*
         * we do not need to set the r->headers_out.location->hash and
         * r->headers_out.location->key fields
         */
        if (r->args.len == 0) {//如果客户端请求没用带参数，比如请求是: GET /AAA/BBB/
            r->headers_out.location->value = clcf->name;
        } else {//如果客户端请求有带参数，比如请求是: GET /AAA/BBB/?query=word，则需要将参数也带在location后面
            len = clcf->name.len + 1 + r->args.len;//计算301的前面写死的长度，+参数长度，下面申请那么大的空间。
            p = ngx_pnalloc(r->pool, len);
            if (p == NULL) {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return NGX_OK;
            }
            r->headers_out.location->value.len = len;//总长度
            r->headers_out.location->value.data = p;//记录字符串地址
            p = ngx_cpymem(p, clcf->name.data, clcf->name.len);//拷贝数据
            *p++ = '?';
            ngx_memcpy(p, r->args.data, r->args.len);//参数得拷贝了，给客户端传递。
        }
		//到这里，结束这个链接。返回301永久重定向。
        ngx_http_finalize_request(r, NGX_HTTP_MOVED_PERMANENTLY);
        return NGX_OK;
    }

    r->phase_handler++;
    return NGX_AGAIN;
}


ngx_int_t
ngx_http_core_post_rewrite_phase(ngx_http_request_t *r,  ngx_http_phase_handler_t *ph)
{//判断一下是否内部重定向超过11次。没做其他事情。
    ngx_http_core_srv_conf_t  *cscf;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "post rewrite phase: %ui", r->phase_handler);

    if (!r->uri_changed) {
        r->phase_handler++;
        return NGX_AGAIN;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "uri changes: %d", r->uri_changes);
    /*
     * gcc before 3.3 compiles the broken code for
     *     if (r->uri_changes-- == 0)
     * if the r->uri_changes is defined as
     *     unsigned  uri_changes:4
     */
    r->uri_changes--;
    if (r->uri_changes == 0) {//重定向超过11次了，中断请求。
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "rewrite or internal redirection cycle while processing \"%V\"", &r->uri);
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_OK;
    }

    r->phase_handler = ph->next;//将其设置为下一个处理句柄，其实是配置查找相关的。checker为ngx_http_core_find_config_phase

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
    r->loc_conf = cscf->ctx->loc_conf;

    return NGX_AGAIN;
}


ngx_int_t
ngx_http_core_access_phase(ngx_http_request_t *r, ngx_http_phase_handler_t *ph)
{//返回NGX_OK将终止后续的过程处理。
    ngx_int_t                  rc;
    ngx_http_core_loc_conf_t  *clcf;

    if (r != r->main) {//如果不是主请求，则不用特殊处理直接转到下一步。
        r->phase_handler = ph->next;
        return NGX_AGAIN;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,  "access phase: %ui", r->phase_handler);
    rc = ph->handler(r);//ngx_http_access_handler 和ngx_http_auth_basic_handler等
    if (rc == NGX_DECLINED) {
        r->phase_handler++;
        return NGX_AGAIN;
    }

    if (rc == NGX_AGAIN || rc == NGX_DONE) {
        return NGX_OK;//如果handler返回AGAIN或者DONE，就返回NGX_OK,从而终止后续过程的处理。AGAIN怎么也返回OK呢
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->satisfy == NGX_HTTP_SATISFY_ALL) {//安全配置中，运许所有的，那容易了。返回继续处理下一个。
        if (rc == NGX_OK) {
            r->phase_handler++;
            return NGX_AGAIN;
        }

    } else {
        if (rc == NGX_OK) {
            r->access_code = 0;//access_code 这个主要是传递给下一个phase，post_access phase进行处理的。 
            if (r->headers_out.www_authenticate) {
                r->headers_out.www_authenticate->hash = 0;
            }

            r->phase_handler = ph->next;
            return NGX_AGAIN;
        }

        if (rc == NGX_HTTP_FORBIDDEN || rc == NGX_HTTP_UNAUTHORIZED) {
            r->access_code = rc;

            r->phase_handler++;
            return NGX_AGAIN;
        }
    }

    /* rc == NGX_ERROR || rc == NGX_HTTP_...  */

    ngx_http_finalize_request(r, rc);
    return NGX_OK;
}


ngx_int_t
ngx_http_core_post_access_phase(ngx_http_request_t *r, ngx_http_phase_handler_t *ph){
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "post access phase: %ui", r->phase_handler);
    if (r->access_code) {
        if (r->access_code == NGX_HTTP_FORBIDDEN) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "access forbidden by rule");
        }
        ngx_http_finalize_request(r, r->access_code);
        return NGX_OK;
    }

    r->phase_handler++;//去处理下一个过程回调。
    return NGX_AGAIN;
}


ngx_int_t
ngx_http_core_try_files_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph)
{
    size_t                        len, root, alias, reserve, allocated;
    u_char                       *p, *name;
    ngx_str_t                     path, args;
    ngx_uint_t                    test_dir;
    ngx_http_try_file_t          *tf;
    ngx_open_file_info_t          of;
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t      e;
    ngx_http_core_loc_conf_t     *clcf;
    ngx_http_script_len_code_pt   lcode;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "try files phase: %ui", r->phase_handler);

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->try_files == NULL) {
        r->phase_handler++;
        return NGX_AGAIN;
    }

    allocated = 0;
    root = 0;
    name = NULL;
    /* suppress MSVC warning */
    path.data = NULL;

    tf = clcf->try_files;

    alias = clcf->alias;

    for ( ;; ) {

        if (tf->lengths) {
            ngx_memzero(&e, sizeof(ngx_http_script_engine_t));

            e.ip = tf->lengths->elts;
            e.request = r;

            /* 1 is for terminating '\0' as in static names */
            len = 1;

            while (*(uintptr_t *) e.ip) {
                lcode = *(ngx_http_script_len_code_pt *) e.ip;
                len += lcode(&e);
            }

        } else {
            len = tf->name.len;
        }

        /* 16 bytes are preallocation */
        reserve = ngx_abs((ssize_t) (len - r->uri.len)) + alias + 16;

        if (reserve > allocated) {

            /* we just need to allocate path and to copy a root */

            if (ngx_http_map_uri_to_path(r, &path, &root, reserve) == NULL) {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return NGX_OK;
            }

            name = path.data + root;
            allocated = path.len - root - (r->uri.len - alias);
         }

        if (tf->values == NULL) {

            /* tf->name.len includes the terminating '\0' */

            ngx_memcpy(name, tf->name.data, tf->name.len);

            path.len = (name + tf->name.len - 1) - path.data;

        } else {
            e.ip = tf->values->elts;
            e.pos = name;
            e.flushed = 1;

            while (*(uintptr_t *) e.ip) {
                code = *(ngx_http_script_code_pt *) e.ip;
                code((ngx_http_script_engine_t *) &e);
            }

            path.len = e.pos - path.data;

            *e.pos = '\0';

            if (alias && ngx_strncmp(name, clcf->name.data, alias) == 0) {
                ngx_memcpy(name, name + alias, len - alias);
                path.len -= alias;
            }
        }

        test_dir = tf->test_dir;

        tf++;

        ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "try to use %s: \"%s\" \"%s\"",
                       test_dir ? "dir" : "file", name, path.data);

        if (tf->lengths == NULL && tf->name.len == 0) {

            if (tf->code) {
                ngx_http_finalize_request(r, tf->code);
                return NGX_OK;
            }

            path.len -= root;
            path.data += root;

            if (path.data[0] == '@') {
                (void) ngx_http_named_location(r, &path);

            } else {
                ngx_http_split_args(r, &path, &args);

                (void) ngx_http_internal_redirect(r, &path, &args);
            }

            ngx_http_finalize_request(r, NGX_DONE);
            return NGX_OK;
        }

        ngx_memzero(&of, sizeof(ngx_open_file_info_t));

        of.directio = clcf->directio;
        of.valid = clcf->open_file_cache_valid;
        of.min_uses = clcf->open_file_cache_min_uses;
        of.test_only = 1;
        of.errors = clcf->open_file_cache_errors;
        of.events = clcf->open_file_cache_events;

        if (ngx_open_cached_file(clcf->open_file_cache, &path, &of, r->pool)
            != NGX_OK)
        {
            if (of.err != NGX_ENOENT
                && of.err != NGX_ENOTDIR
                && of.err != NGX_ENAMETOOLONG)
            {
                ngx_log_error(NGX_LOG_CRIT, r->connection->log, of.err,
                              "%s \"%s\" failed", of.failed, path.data);
            }

            continue;
        }

        if (of.is_dir && !test_dir) {
            continue;
        }

        path.len -= root;
        path.data += root;

        if (!alias) {
            r->uri = path;

#if (NGX_PCRE)
        } else if (clcf->regex) {
            if (!test_dir) {
                r->uri = path;
                r->add_uri_to_alias = 1;
            }
#endif
        } else {
            r->uri.len = alias + path.len;
            r->uri.data = ngx_pnalloc(r->pool, r->uri.len);
            if (r->uri.data == NULL) {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return NGX_OK;
            }

            p = ngx_copy(r->uri.data, clcf->name.data, alias);
            ngx_memcpy(p, name, path.len);
        }

        ngx_http_set_exten(r);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "try file uri: \"%V\"", &r->uri);

        r->phase_handler++;
        return NGX_AGAIN;
    }

    /* not reached */
}


ngx_int_t
ngx_http_core_content_phase(ngx_http_request_t *r,   ngx_http_phase_handler_t *ph)
{
    size_t     root;
    ngx_int_t  rc;
    ngx_str_t  path;

    if (r->content_handler) {
//如果有content_handler,就直接调用就行了.比如如果是FCGI，在遇到配置fastcgi_pass   127.0.0.1:8777;的时候
//会调用ngx_http_fastcgi_pass函数，注册本location的处理hander为ngx_http_fastcgi_handler。
//从而在ngx_http_update_location_config里面会更新content_handler指针为当前loc所对应的指针。
        r->write_event_handler = ngx_http_request_empty_handler;
        ngx_http_finalize_request(r, r->content_handler(r));
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "content phase: %ui", r->phase_handler);
    rc = ph->handler(r);//这个是什么?
    if (rc != NGX_DECLINED) {
        ngx_http_finalize_request(r, rc);
        return NGX_OK;
    }
    /* rc == NGX_DECLINED */

    ph++;
    if (ph->checker) {
        r->phase_handler++;
        return NGX_AGAIN;
    }
    /* no content handler was found */
    if (r->uri.data[r->uri.len - 1] == '/') {//如果后面是一个/，那么进行重定向到默认index页面
        if (ngx_http_map_uri_to_path(r, &path, &root, 0) != NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "directory index of \"%s\" is forbidden", path.data);
        }
        ngx_http_finalize_request(r, NGX_HTTP_FORBIDDEN);
        return NGX_OK;
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "no handler found");
    ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
    return NGX_OK;
}


void ngx_http_update_location_config(ngx_http_request_t *r)
{//更新各种配置。根据loc_conf的配置，将对应的所需数据设置到r请求结构体上面去。
//主要的还有 r->content_handler = clcf->handler;，设置回调。
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (r->method & clcf->limit_except) {
        r->loc_conf = clcf->limit_except_loc_conf;
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    }
    if (r == r->main) {
        r->connection->log->file = clcf->error_log->file;
        if (!(r->connection->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {
            r->connection->log->log_level = clcf->error_log->log_level;
        }
    }

    if ((ngx_io.flags & NGX_IO_SENDFILE) && clcf->sendfile) {
        r->connection->sendfile = 1;//用sendfile发送
    } else {
        r->connection->sendfile = 0;
    }

    if (clcf->client_body_in_file_only) {
        r->request_body_in_file_only = 1;
        r->request_body_in_persistent_file = 1;
        r->request_body_in_clean_file = clcf->client_body_in_file_only == NGX_HTTP_REQUEST_BODY_FILE_CLEAN;
        r->request_body_file_log_level = NGX_LOG_NOTICE;

    } else {
        r->request_body_file_log_level = NGX_LOG_WARN;
    }
    r->request_body_in_single_buf = clcf->client_body_in_single_buffer;
    if (r->keepalive) {
        if (clcf->keepalive_timeout == 0) {
            r->keepalive = 0;
        } else if (r->connection->requests >= clcf->keepalive_requests) {
            r->keepalive = 0;
        }
    }
    if (!clcf->tcp_nopush) {
        /* disable TCP_NOPUSH/TCP_CORK use */
        r->connection->tcp_nopush = NGX_TCP_NOPUSH_DISABLED;
    }
    if (r->limit_rate == 0) {
        r->limit_rate = clcf->limit_rate;
    }
    if (clcf->handler) {//如果配置模块设置了其handler，那么将其设置到content_handler指针上面，以备待会在处理phase的时候，调用之。
//ngx_http_fastcgi_handler就在这里设置了，于是在ngx_http_core_content_phase就会调用这个FCGI函数，然后请求就交给对应的处理了
//proxy 模块为ngx_http_proxy_handler。
        r->content_handler = clcf->handler;
    }
}


/*
 * NGX_OK       - exact or regex match
 * NGX_DONE     - auto redirect
 * NGX_AGAIN    - inclusive match
 * NGX_ERROR    - regex error
 * NGX_DECLINED - no match
 */
static ngx_int_t
ngx_http_core_find_location(ngx_http_request_t *r)
{//功能:找出指定的URI对应的那个location结构，设置到r->loc_conf数组上面。
//第一步，查询非精确匹配的static_locations三叉树里面有没有匹配成功的；
//第二步，正则匹配；同时如果匹配成功了，顺便看一下有没有嵌套的location有机会的。
//这个函数的r->loc_conf实际上有控制递归的作用。更新后，所有的环境信息都更新了，
    ngx_int_t                  rc;
    ngx_http_core_loc_conf_t  *pclcf;
#if (NGX_PCRE)
    ngx_int_t                  n;
    ngx_uint_t                 noregex;
    ngx_http_core_loc_conf_t  *clcf, **clcfp;
    noregex = 0;
#endif
    pclcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    rc = ngx_http_core_find_static_location(r, pclcf->static_locations);
	//找到对应的配置location后，将loc_conf数组首地址设置到r->loc_conf指针上面，这样就切换了location啦。

    if (rc == NGX_AGAIN) {//这里代表的是非exact精确匹配成功的。肯定到这还不是正则成功的。
#if (NGX_PCRE)
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
        noregex = clcf->noregex;//^~ 开头表示uri以某个常规字符串开头，理解为匹配 url路径即可。为1表示常规字符串开头。不是正则匹配。
#endif
        /* look up nested locations */
        rc = ngx_http_core_find_location(r);//如果是非精确匹配成功的，下面看看有没有嵌套的
    }
    if (rc == NGX_OK || rc == NGX_DONE) {
        return rc;//成功找到了r->loc_conf已经设置为所对应的那个locations的loc_conf结构了。
    }
    /* rc == NGX_DECLINED or rc == NGX_AGAIN in nested location */
#if (NGX_PCRE)
    if (noregex == 0 && pclcf->regex_locations) {//用了正则表达式，而且呢，regex_locations正则表达式列表里面有货，那就匹配之。
        for (clcfp = pclcf->regex_locations; *clcfp; clcfp++) {//对每一个正则表达式，匹配之。
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "test location: ~ \"%V\"", &(*clcfp)->name);
            n = ngx_http_regex_exec(r, (*clcfp)->regex, &r->uri);
            if (n == NGX_OK) {
                r->loc_conf = (*clcfp)->loc_conf;//匹配成功，记录。
                /* look up nested locations */
                rc = ngx_http_core_find_location(r);
				//看看是否还有嵌套的。注意，这个再次进去的时候，由于r->loc_conf已经被设置为新的location了，所以其实这个算是递归进入了。
                return (rc == NGX_ERROR) ? rc : NGX_OK;
            }
            if (n == NGX_DECLINED) {
                continue;
            }
            return NGX_ERROR;
        }
    }
#endif
    return rc;
}


/*
 * NGX_OK       - exact match
 * NGX_DONE     - auto redirect
 * NGX_AGAIN    - inclusive match
 * NGX_DECLINED - no match
 */

static ngx_int_t
ngx_http_core_find_static_location(ngx_http_request_t *r, ngx_http_location_tree_node_t *node)
{//node为pclcf->static_locations，也就是三叉树根
//找出r->uri所请求的URI所对应的location，注意这个肯定是一般的location，正则，named，nonamed都不在这里面的。
//找到对应的配置location后，将loc_conf数组首地址设置到r->loc_conf指针上面，这样就切换了location啦。
    u_char     *uri;
    size_t      len, n;
    ngx_int_t   rc, rv;

    len = r->uri.len;
    uri = r->uri.data;
    rv = NGX_DECLINED;
    for ( ;; ) {
        if (node == NULL) {//到头了。
            return rv;
        }
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "test location: \"%*s\"", node->len, node->name);
        n = (len <= (size_t) node->len) ? len : node->len;//取最小长度
        rc = ngx_filename_cmp(uri, node->name, n);//就是strncasecmp
        if (rc != 0) {
            node = (rc < 0) ? node->left : node->right;//如果不相等，则决定往左走还是往右走。
            continue;
        }
		//到这里一定是前缀相等了。
        if (len > (size_t) node->len) {//只不过，URI长度更长，所以还得看看子节点哦
            if (node->inclusive) {//如果该节点是inclusive类型的非精确匹配节点。
                r->loc_conf = node->inclusive->loc_conf;//先记录一下loc_conf，待会要是没有更好的匹配了，这个就是了。
                rv = NGX_AGAIN;

                node = node->tree;//去尝试一下子树吧，他们有相同的前缀。
                uri += n;//跳过前面已经匹配成功的。部分。
                len -= n;
                continue;
            }
            /* exact only */
            node = node->right;//URI长度更长，只能再看看右边的节点了。
            continue;
        }

        if (len == (size_t) node->len) {//长度相等。
            if (node->exact) {//又是精确匹配，那必然就OK了。
                r->loc_conf = node->exact->loc_conf;//下面果断更新配置为exact所代表的loc_conf。
                return NGX_OK;
            } else {//不是精确匹配成功的。
                r->loc_conf = node->inclusive->loc_conf;
                return NGX_AGAIN;
            }
        }
        /* len < node->len */
        if (len + 1 == (size_t) node->len && node->auto_redirect) {
			//URI长度+1正好等于节点长度，并且呢，有auto_redirect标志，那就可以了，
			//上层会根据返回值，给客户端返回301重定向的。
            r->loc_conf = (node->exact) ? node->exact->loc_conf:  node->inclusive->loc_conf;
            rv = NGX_DONE;
        }
        node = node->left;//往左。
    }
}


void *
ngx_http_test_content_type(ngx_http_request_t *r, ngx_hash_t *types_hash)
{
    u_char      c, *lowcase;
    size_t      len;
    ngx_uint_t  i, hash;

    if (types_hash->size == 0) {
        return (void *) 4;
    }

    if (r->headers_out.content_type.len == 0) {
        return NULL;
    }

    len = r->headers_out.content_type_len;

    if (r->headers_out.content_type_lowcase == NULL) {

        lowcase = ngx_pnalloc(r->pool, len);
        if (lowcase == NULL) {
            return NULL;
        }

        r->headers_out.content_type_lowcase = lowcase;

        hash = 0;

        for (i = 0; i < len; i++) {
            c = ngx_tolower(r->headers_out.content_type.data[i]);
            hash = ngx_hash(hash, c);
            lowcase[i] = c;
        }

        r->headers_out.content_type_hash = hash;
    }

    return ngx_hash_find(types_hash, r->headers_out.content_type_hash,
                         r->headers_out.content_type_lowcase, len);
}


ngx_int_t
ngx_http_set_content_type(ngx_http_request_t *r)
{//自动根据后缀名，如果ngx_http_core_default_types初始化了后缀，
//这里会查找自动匹配合适的content_type，如果客户端没有发送的话。
    u_char                     c, *exten;
    ngx_str_t                 *type;
    ngx_uint_t                 i, hash;
    ngx_http_core_loc_conf_t  *clcf;

    if (r->headers_out.content_type.len) {//已经有了，直接返回。
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (r->exten.len) {//如果请求有后缀
        hash = 0;
        for (i = 0; i < r->exten.len; i++) {//循环用每个后缀名字符计算hash
            c = r->exten.data[i];
            if (c >= 'A' && c <= 'Z') {//变成小写。
                exten = ngx_pnalloc(r->pool, r->exten.len);
                if (exten == NULL) {
                    return NGX_ERROR;
                }
                hash = ngx_hash_strlow(exten, r->exten.data, r->exten.len);
                r->exten.data = exten;
                break;
            }
            hash = ngx_hash(hash, c);//最简单的对每个字符做hash . ((ngx_uint_t) key * 31 + c)
        }
		//看看是否有存在映射的。映射在这里ngx_http_core_default_types。
        type = ngx_hash_find(&clcf->types_hash, hash, r->exten.data, r->exten.len);
        if (type) {
            r->headers_out.content_type_len = type->len;
            r->headers_out.content_type = *type;
            return NGX_OK;
        }
    }
    r->headers_out.content_type_len = clcf->default_type.len;
    r->headers_out.content_type = clcf->default_type;
    return NGX_OK;
}


void
ngx_http_set_exten(ngx_http_request_t *r)
{
    ngx_int_t  i;

    ngx_str_null(&r->exten);

    for (i = r->uri.len - 1; i > 1; i--) {
        if (r->uri.data[i] == '.' && r->uri.data[i - 1] != '/') {

            r->exten.len = r->uri.len - i - 1;
            r->exten.data = &r->uri.data[i + 1];

            return;

        } else if (r->uri.data[i] == '/') {
            return;
        }
    }

    return;
}


ngx_int_t
ngx_http_send_response(ngx_http_request_t *r, ngx_uint_t status,  ngx_str_t *ct, ngx_http_complex_value_t *cv)
{//根据复杂表达式获取其值，然后确定是重定向还是要发送响应，将头部数据和body数据发送给客户端。
    ngx_int_t     rc;
    ngx_str_t     val;
    ngx_buf_t    *b;
    ngx_chain_t   out;

    r->headers_out.status = status;
    if (status == NGX_HTTP_NO_CONTENT) {
        r->header_only = 1;
        return ngx_http_send_header(r);
    }
	//根据val复杂表达式结构，获取其代表的目标值，存入value.
    if (ngx_http_complex_value(r, cv, &val) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    if (status >= NGX_HTTP_MOVED_PERMANENTLY && status <= NGX_HTTP_SEE_OTHER) {
		//重定向，val上就是目标URL吧。
        r->headers_out.location = ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.location == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        r->headers_out.location->hash = 1;
        ngx_str_set(&r->headers_out.location->key, "Location");
        r->headers_out.location->value = val;
        return status;
    }
	//不是重定向，肿么办，这个事一坨数据，要echo 回去的。
    r->headers_out.content_length_n = val.len;
    if (ct) {
        r->headers_out.content_type_len = ct->len;
        r->headers_out.content_type = *ct;
    } else {
        if (ngx_http_set_content_type(r) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }
    if (r->method == NGX_HTTP_HEAD || (r != r->main && val.len == 0)) {
        return ngx_http_send_header(r);//客户端只要header,那么就发送head吧
    }

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	//要发送数据，所以得逐步相关的结构了，输出buf
    b->pos = val.data;//要输出的内容。
    b->last = val.data + val.len;
    b->memory = val.len ? 1 : 0;
    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;
    out.buf = b;//就一坨数据，一块就够了。因此这个链表只有一个节点。
    out.next = NULL;
    rc = ngx_http_send_header(r);//发视头部数据。

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
	//BODY发送函数，调用ngx_http_top_body_filter一个个将他们发送出去
    return ngx_http_output_filter(r, &out);
}


ngx_int_t
ngx_http_send_header(ngx_http_request_t *r)
{
    if (r->err_status) {//如果有错误发生，则发送错误状态。
        r->headers_out.status = r->err_status;
        r->headers_out.status_line.len = 0;
    }
	//第一个是ngx_http_not_modified_header_filter，后面一个个往后掉用。每个函数独立一个文件，那里面有个静态变量记录它的上一个filter是谁。
    return ngx_http_top_header_filter(r);//最后一个是ngx_http_header_filter_module模块里面的发送模块
}


ngx_int_t
ngx_http_output_filter(ngx_http_request_t *r, ngx_chain_t *in)
{//BODY发送函数，调用ngx_http_top_body_filter一个个将他们发送出去
//参数in为u->out_bufs，也就是待发送的数据，本函数完成后，u->out_bufs的数据将移动到busy_bufs
    ngx_int_t          rc;
    ngx_connection_t  *c;

    c = r->connection;
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,  "http output filter \"%V?%V\"", &r->uri, &r->args);
    rc = ngx_http_top_body_filter(r, in);//一个个body filter调用，最终发送出数据。第一个是ngx_http_range_body_filter
    //最后一个body filter 是ngx_http_write_filter
    if (rc == NGX_ERROR) {
        /* NGX_ERROR may be returned by any filter */
        c->error = 1;
    }

    return rc;
}


u_char *
ngx_http_map_uri_to_path(ngx_http_request_t *r, ngx_str_t *path, size_t *root_length, size_t reserved)
{
    u_char                    *last;
    size_t                     alias;
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    alias = clcf->alias;
    if (alias && !r->valid_location) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "\"alias\" could not be used in location \"%V\" where URI was rewritten", &clcf->name);
        return NULL;
    }
    if (clcf->root_lengths == NULL) {
        *root_length = clcf->root.len;//设置好root 目录的长度
        path->len = clcf->root.len + reserved + r->uri.len - alias + 1;
        path->data = ngx_pnalloc(r->pool, path->len);
        if (path->data == NULL) {
            return NULL;
        }
        last = ngx_copy(path->data, clcf->root.data, clcf->root.len);//拷贝前面的路径根部分
    } else {
#if (NGX_PCRE)
        ngx_uint_t  captures;
        captures = alias && clcf->regex;
        reserved += captures ? (r->add_uri_to_alias ? r->uri.len + 1 : 1) : r->uri.len - alias + 1;
#else
        reserved += r->uri.len - alias + 1;
#endif
		//编译一下这些变量，计算其值。
        if (ngx_http_script_run(r, path, clcf->root_lengths->elts, reserved,  clcf->root_values->elts) == NULL) {
            return NULL;
        }
        if (ngx_conf_full_name((ngx_cycle_t *) ngx_cycle, path, 0) != NGX_OK) {
            return NULL;//将name参数转为绝对路径。
        }
        *root_length = path->len - reserved;
        last = path->data + *root_length;
#if (NGX_PCRE)
        if (captures) {
            if (!r->add_uri_to_alias) {
                *last = '\0';
                return last;
            }
            alias = 0;
        }
#endif
    }
    last = ngx_cpystrn(last, r->uri.data + alias, r->uri.len - alias + 1);
    return last;
}


ngx_int_t
ngx_http_auth_basic_user(ngx_http_request_t *r)
{
    ngx_str_t   auth, encoded;
    ngx_uint_t  len;

    if (r->headers_in.user.len == 0 && r->headers_in.user.data != NULL) {
        return NGX_DECLINED;
    }

    if (r->headers_in.authorization == NULL) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    encoded = r->headers_in.authorization->value;

    if (encoded.len < sizeof("Basic ") - 1
        || ngx_strncasecmp(encoded.data, (u_char *) "Basic ",
                           sizeof("Basic ") - 1)
           != 0)
    {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    encoded.len -= sizeof("Basic ") - 1;
    encoded.data += sizeof("Basic ") - 1;

    while (encoded.len && encoded.data[0] == ' ') {
        encoded.len--;
        encoded.data++;
    }

    if (encoded.len == 0) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    auth.len = ngx_base64_decoded_length(encoded.len);
    auth.data = ngx_pnalloc(r->pool, auth.len + 1);
    if (auth.data == NULL) {
        return NGX_ERROR;
    }

    if (ngx_decode_base64(&auth, &encoded) != NGX_OK) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    auth.data[auth.len] = '\0';

    for (len = 0; len < auth.len; len++) {
        if (auth.data[len] == ':') {
            break;
        }
    }

    if (len == 0 || len == auth.len) {
        r->headers_in.user.data = (u_char *) "";
        return NGX_DECLINED;
    }

    r->headers_in.user.len = len;
    r->headers_in.user.data = auth.data;
    r->headers_in.passwd.len = auth.len - len - 1;
    r->headers_in.passwd.data = &auth.data[len + 1];

    return NGX_OK;
}


#if (NGX_HTTP_GZIP)

ngx_int_t
ngx_http_gzip_ok(ngx_http_request_t *r)
{
    time_t                     date, expires;
    ngx_uint_t                 p;
    ngx_array_t               *cc;
    ngx_table_elt_t           *e, *d;
    ngx_http_core_loc_conf_t  *clcf;

    r->gzip_tested = 1;

    if (r != r->main
        || r->headers_in.accept_encoding == NULL
        || ngx_strcasestrn(r->headers_in.accept_encoding->value.data,
                           "gzip", 4 - 1)
           == NULL

        /*
         * if the URL (without the "http://" prefix) is longer than 253 bytes,
         * then MSIE 4.x can not handle the compressed stream - it waits
         * too long, hangs up or crashes
         */

        || (r->headers_in.msie4 && r->unparsed_uri.len > 200))
    {
        return NGX_DECLINED;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (r->headers_in.msie6 && clcf->gzip_disable_msie6) {
        return NGX_DECLINED;
    }

    if (r->http_version < clcf->gzip_http_version) {
        return NGX_DECLINED;
    }

    if (r->headers_in.via == NULL) {
        goto ok;
    }

    p = clcf->gzip_proxied;

    if (p & NGX_HTTP_GZIP_PROXIED_OFF) {
        return NGX_DECLINED;
    }

    if (p & NGX_HTTP_GZIP_PROXIED_ANY) {
        goto ok;
    }

    if (r->headers_in.authorization && (p & NGX_HTTP_GZIP_PROXIED_AUTH)) {
        goto ok;
    }

    e = r->headers_out.expires;

    if (e) {

        if (!(p & NGX_HTTP_GZIP_PROXIED_EXPIRED)) {
            return NGX_DECLINED;
        }

        expires = ngx_http_parse_time(e->value.data, e->value.len);
        if (expires == NGX_ERROR) {
            return NGX_DECLINED;
        }

        d = r->headers_out.date;

        if (d) {
            date = ngx_http_parse_time(d->value.data, d->value.len);
            if (date == NGX_ERROR) {
                return NGX_DECLINED;
            }

        } else {
            date = ngx_time();
        }

        if (expires < date) {
            goto ok;
        }

        return NGX_DECLINED;
    }

    cc = &r->headers_out.cache_control;

    if (cc->elts) {

        if ((p & NGX_HTTP_GZIP_PROXIED_NO_CACHE)
            && ngx_http_parse_multi_header_lines(cc, &ngx_http_gzip_no_cache,
                                                 NULL)
               >= 0)
        {
            goto ok;
        }

        if ((p & NGX_HTTP_GZIP_PROXIED_NO_STORE)
            && ngx_http_parse_multi_header_lines(cc, &ngx_http_gzip_no_store,
                                                 NULL)
               >= 0)
        {
            goto ok;
        }

        if ((p & NGX_HTTP_GZIP_PROXIED_PRIVATE)
            && ngx_http_parse_multi_header_lines(cc, &ngx_http_gzip_private,
                                                 NULL)
               >= 0)
        {
            goto ok;
        }

        return NGX_DECLINED;
    }

    if ((p & NGX_HTTP_GZIP_PROXIED_NO_LM) && r->headers_out.last_modified) {
        return NGX_DECLINED;
    }

    if ((p & NGX_HTTP_GZIP_PROXIED_NO_ETAG) && r->headers_out.etag) {
        return NGX_DECLINED;
    }

ok:

#if (NGX_PCRE)

    if (clcf->gzip_disable && r->headers_in.user_agent) {

        if (ngx_regex_exec_array(clcf->gzip_disable,
                                 &r->headers_in.user_agent->value,
                                 r->connection->log)
            != NGX_DECLINED)
        {
            return NGX_DECLINED;
        }
    }

#endif

    r->gzip_ok = 1;

    return NGX_OK;
}

#endif


ngx_int_t
ngx_http_subrequest(ngx_http_request_t *r, ngx_str_t *uri, ngx_str_t *args, ngx_http_request_t **psr,
					ngx_http_post_subrequest_t *ps, ngx_uint_t flags)
{
    ngx_time_t                    *tp;
    ngx_connection_t              *c;
    ngx_http_request_t            *sr;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_http_postponed_request_t  *pr, *p;

    r->main->subrequests--;

    if (r->main->subrequests == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "subrequests cycle while processing \"%V\"", uri);
        r->main->subrequests = 1;
        return NGX_ERROR;
    }

    sr = ngx_pcalloc(r->pool, sizeof(ngx_http_request_t));
    if (sr == NULL) {
        return NGX_ERROR;
    }

    sr->signature = NGX_HTTP_MODULE;
    c = r->connection;
    sr->connection = c;

    sr->ctx = ngx_pcalloc(r->pool, sizeof(void *) * ngx_http_max_module);
    if (sr->ctx == NULL) {
        return NGX_ERROR;
    }

    if (ngx_list_init(&sr->headers_out.headers, r->pool, 20, sizeof(ngx_table_elt_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
    sr->main_conf = cscf->ctx->main_conf;
    sr->srv_conf = cscf->ctx->srv_conf;
    sr->loc_conf = cscf->ctx->loc_conf;

    sr->pool = r->pool;

    sr->headers_in = r->headers_in;

    ngx_http_clear_content_length(sr);
    ngx_http_clear_accept_ranges(sr);
    ngx_http_clear_last_modified(sr);

    sr->request_body = r->request_body;

    sr->method = NGX_HTTP_GET;
    sr->http_version = r->http_version;

    sr->request_line = r->request_line;
    sr->uri = *uri;

    if (args) {
        sr->args = *args;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "http subrequest \"%V?%V\"", uri, &sr->args);
    sr->subrequest_in_memory = (flags & NGX_HTTP_SUBREQUEST_IN_MEMORY) != 0;
    sr->waited = (flags & NGX_HTTP_SUBREQUEST_WAITED) != 0;

    sr->unparsed_uri = r->unparsed_uri;
    sr->method_name = ngx_http_core_get_method;
    sr->http_protocol = r->http_protocol;

    ngx_http_set_exten(sr);

    sr->main = r->main;
    sr->parent = r;
    sr->post_subrequest = ps;
    sr->read_event_handler = ngx_http_request_empty_handler;
    sr->write_event_handler = ngx_http_handler;

    if (c->data == r && r->postponed == NULL) {
        c->data = sr;
    }

    sr->variables = r->variables;

    sr->log_handler = r->log_handler;

    pr = ngx_palloc(r->pool, sizeof(ngx_http_postponed_request_t));
    if (pr == NULL) {
        return NGX_ERROR;
    }

    pr->request = sr;
    pr->out = NULL;
    pr->next = NULL;

    if (r->postponed) {
        for (p = r->postponed; p->next; p = p->next) { /* void */ }
        p->next = pr;

    } else {
        r->postponed = pr;
    }

    sr->internal = 1;

    sr->discard_body = r->discard_body;
    sr->expect_tested = 1;
    sr->main_filter_need_in_memory = r->main_filter_need_in_memory;

    sr->uri_changes = NGX_HTTP_MAX_URI_CHANGES + 1;

    tp = ngx_timeofday();
    r->start_sec = tp->sec;
    r->start_msec = tp->msec;

    r->main->subrequests++;
    r->main->count++;

    *psr = sr;

    return ngx_http_post_request(sr, NULL);
}


ngx_int_t
ngx_http_internal_redirect(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args)
{
    ngx_http_core_srv_conf_t  *cscf;

    r->uri_changes--;//初始设置为NGX_HTTP_MAX_URI_CHANGES，即最多重定向10此，这是为了避免循环重定向的问题。
    if (r->uri_changes == 0) {//
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "rewrite or internal redirection cycle "
                      "while internal redirect to \"%V\"", uri);
        r->main->count++;
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_DONE;
    }
    r->uri = *uri;
    if (args) {
        r->args = *args;
    } else {
        ngx_str_null(&r->args);
    }
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,  "internal redirect: \"%V?%V\"", uri, &r->args);
    ngx_http_set_exten(r);
    /* clear the modules contexts */
    ngx_memzero(r->ctx, sizeof(void *) * ngx_http_max_module);
    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
    r->loc_conf = cscf->ctx->loc_conf;
    ngx_http_update_location_config(r);
#if (NGX_HTTP_CACHE)
    r->cache = NULL;
#endif
    r->internal = 1;//设置请求为内部重定向状态。通知ngx_http_handler,进行间断选择的时候从server_rewrite_index开始进行循环处理，不然又回去了
    r->add_uri_to_alias = 0;
    r->main->count++;

    ngx_http_handler(r);

    return NGX_DONE;
}


ngx_int_t
ngx_http_named_location(ngx_http_request_t *r, ngx_str_t *name)
{
    ngx_http_core_srv_conf_t    *cscf;
    ngx_http_core_loc_conf_t   **clcfp;
    ngx_http_core_main_conf_t   *cmcf;

    r->main->count++;

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

    if (cscf->named_locations) {

        for (clcfp = cscf->named_locations; *clcfp; clcfp++) {

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "test location: \"%V\"", &(*clcfp)->name);

            if (name->len != (*clcfp)->name.len
                || ngx_strncmp(name->data, (*clcfp)->name.data, name->len) != 0)
            {
                continue;
            }

            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "using location: %V \"%V?%V\"",
                           name, &r->uri, &r->args);

            r->internal = 1;
            r->content_handler = NULL;
            r->loc_conf = (*clcfp)->loc_conf;

            ngx_http_update_location_config(r);

            cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

            r->phase_handler = cmcf->phase_engine.location_rewrite_index;

            ngx_http_core_run_phases(r);

            return NGX_DONE;
        }
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "could not find named location \"%V\"", name);

    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);

    return NGX_DONE;
}


ngx_http_cleanup_t *
ngx_http_cleanup_add(ngx_http_request_t *r, size_t size)
{
    ngx_http_cleanup_t  *cln;
    r = r->main;
    cln = ngx_palloc(r->pool, sizeof(ngx_http_cleanup_t));
    if (cln == NULL) {
        return NULL;
    }
    if (size) {
        cln->data = ngx_palloc(r->pool, size);
        if (cln->data == NULL) {
            return NULL;
        }
    } else {
        cln->data = NULL;
    }

    cln->handler = NULL;
    cln->next = r->cleanup;//环形的，干嘛的呢
    r->cleanup = cln;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http cleanup add: %p", cln);
    return cln;
}

/*
cf->ctx 指向cycle->conf_ctx[http_module],也就是指向ngx_http_conf_ctx_t的结构。
dummy实际上指向为cf->ctx->main_conf，也就是HTTP的main_conf指针
*/
static char * ngx_http_core_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{//解析到server{}的时候调用这里。
    char                        *rv;
    void                        *mconf;
    ngx_uint_t                   i;
    ngx_conf_t                   pcf;
    ngx_http_module_t           *module;
    struct sockaddr_in          *sin;
    ngx_http_conf_ctx_t         *ctx, *http_ctx;
    ngx_http_listen_opt_t        lsopt;
    ngx_http_core_srv_conf_t    *cscf, **cscfp;
    ngx_http_core_main_conf_t   *cmcf;

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));//申请一个ctx结构
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    http_ctx = cf->ctx;//这2句是为了将server部分的main_conf跟http的共享放一份。
    ctx->main_conf = http_ctx->main_conf;

    /* the server{}'s srv_conf */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    /* the server{}'s loc_conf */
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    for (i = 0; ngx_modules[i]; i++) {//老规矩，各种初始化一下、。
        if (ngx_modules[i]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[i]->ctx;
        if (module->create_srv_conf) {
            mconf = module->create_srv_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->srv_conf[ngx_modules[i]->ctx_index] = mconf;
        }
        if (module->create_loc_conf) {//创建loc配置结构。每个server都有一个份这样的数组
            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->loc_conf[ngx_modules[i]->ctx_index] = mconf;
        }
    }
    /* the server configuration context */
    cscf = ctx->srv_conf[ngx_http_core_module.ctx_index];
    cscf->ctx = ctx;//回指我所属于的这个模块上下文ctx，那么从这里，只要知道了cscf，也就能够知道ctx其所属的ctx。
    cmcf = ctx->main_conf[ngx_http_core_module.ctx_index];//main_conf其实是http的，共享了一份
    cscfp = ngx_array_push(&cmcf->servers);//得到main_conf的core核心配置，其实就是HTTP的servers配置。新建一个元素，用来保存当前的cscf。
    if (cscfp == NULL) {//cmcf->servers的数量不一定等于server_name的数量，因为可能一个server有2个名称。
        return NGX_CONF_ERROR;//gx_http_core_srv_conf_t的server_names成员才记录了当前server的所有域名昵称
    }
    *cscfp = cscf;//将这个cscf的核心srv_conf增加到HTTP的main配置的servers数组里面，也就登记了一下:增加了一个server哈。

    /* parse inside server{} */
    pcf = *cf;//备份上下文结构
    cf->ctx = ctx;//使用当前的，也就是server的ctx解析下面的结构。
    cf->cmd_type = NGX_HTTP_SRV_CONF;
    rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;//还原上下文结构。到此server{}解析完毕了。

    if (rv == NGX_CONF_OK && !cscf->listen) {//如果server{}里面指定里listen 8888;监听端口，则在解析到监听端口的时候就会调用ngx_http_add_listen设置信息
		//如果这个server里面没有显示的指定监听端口，下面默认设置一个80或者8000端口。
        ngx_memzero(&lsopt, sizeof(ngx_http_listen_opt_t));
        sin = &lsopt.u.sockaddr_in;
        sin->sin_family = AF_INET;
#if (NGX_WIN32)
        sin->sin_port = htons(80);
#else
        sin->sin_port = htons((getuid() == 0) ? 80 : 8000);//root用户监听80，其他用户默认监听8000
#endif
        sin->sin_addr.s_addr = INADDR_ANY;
        lsopt.socklen = sizeof(struct sockaddr_in);
        lsopt.backlog = NGX_LISTEN_BACKLOG;
        lsopt.rcvbuf = -1;
        lsopt.sndbuf = -1;
#if (NGX_HAVE_SETFIB)
        lsopt.setfib = -1;
#endif
        lsopt.wildcard = 1;
        (void) ngx_sock_ntop(&lsopt.u.sockaddr, lsopt.addr, NGX_SOCKADDR_STRLEN, 1);
		//将这个默认的监听端口设置到cscf里面，以及http的main_conf的ports数组里面，以及各种将这个端口联系起来。
        if (ngx_http_add_listen(cf, cscf, &lsopt) != NGX_OK) {//比如http里面记录这个端口，然后这个端口记录其同端口不同地址列表，后者再记录对应的cscf.
            return NGX_CONF_ERROR;
        }
    }

    return rv;
}


static char *
ngx_http_core_location(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{//碰到location的时候，调用这里。
    char                      *rv;
    u_char                    *mod;
    size_t                     len;
    ngx_str_t                 *value, *name;
    ngx_uint_t                 i;
    ngx_conf_t                 save;
    ngx_http_module_t         *module;
    ngx_http_conf_ctx_t       *ctx, *pctx;
    ngx_http_core_loc_conf_t  *clcf, *pclcf;
//后面的逻辑看到，ctx指针变成了一个野指针，没有地方索引了，但是，由于ctx->loc_conf 被核心模块的clcf->loc_conf索引，所以实际数据是能找到的。
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));//这个分配的ctx放到哪里去了?
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    pctx = cf->ctx;//记着上一个东东，待会看看他是不是嵌套的。
    ctx->main_conf = pctx->main_conf;//继承顶层的main,srv配置。
    ctx->srv_conf = pctx->srv_conf;
	//但是对于loc_conf，需要自己使用一份出来。
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 0; ngx_modules[i]; i++) {//遍历每一个HTTP模块，如果其有create_loc_conf，则调用之。这是啥意思?
        if (ngx_modules[i]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[i]->ctx;
        if (module->create_loc_conf) {//意思是:大家好，我又碰到了一个location块，麻烦大伙准备一下配置数据，如果你需要的话。
            ctx->loc_conf[ngx_modules[i]->ctx_index] =  module->create_loc_conf(cf);
            if (ctx->loc_conf[ngx_modules[i]->ctx_index] == NULL) {
                 return NGX_CONF_ERROR;
            }
        }
    }
	//clcf会被pclcf->locations所指向。但是刚刚申请的ctx好像没有地方指向它了，变成了一个野指针。
    clcf = ctx->loc_conf[ngx_http_core_module.ctx_index];//得到HTTP老大的loc_conf配置，其实它的conf肯定等于顶层的了，为什么呢，因为他是顶层的。
    clcf->loc_conf = ctx->loc_conf;//这是啥意思，clcf是刚才调用create_loc_conf时返回的。相当于让其依然等于上层的loc_conf.共享即可。
    //上面是说，核心模块的loc_conf就是本模块。在location的解析中，
    //ngx_http_core_loc_conf_t结构(即clcf)的loc_conf成员会保存所有其他module的location配置用到的时候引用方便。然后核心模块clcf正好被locations索引
    //因此在解析配置的时候，通过locations，自然就能找到clcf，进而通过clcf->loc_conf找到刚申请的ctx->loc_conf。而对于main_conf，没用了。
    value = cf->args->elts;
    if (cf->args->nelts == 3) {//location ~ \.php$ {这种处理。
        len = value[1].len;
        mod = value[1].data;
        name = &value[2];

        if (len == 1 && mod[0] == '=') {
            clcf->name = *name;//记住location后面的名字是什么
            clcf->exact_match = 1;
        } else if (len == 2 && mod[0] == '^' && mod[1] == '~') {//^~ 开头表示uri以某个常规字符串开头，理解为匹配 url路径即可。
            clcf->name = *name;
            clcf->noregex = 1;
        } else if (len == 1 && mod[0] == '~') {//大小不敏感的正则匹配
            if (ngx_http_core_regex_location(cf, clcf, name, 0) != NGX_OK) {
                return NGX_CONF_ERROR;//解析正则表达式。保存到clcf的regex变量里面
            }
        } else if (len == 2 && mod[0] == '~' && mod[1] == '*') {//大小写敏感
            if (ngx_http_core_regex_location(cf, clcf, name, 1) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid location modifier \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    } else {//这里表示后面的=,~等符号没有跟第三个参数字符串用空格分开的情况。
        name = &value[1];
        if (name->data[0] == '=') {
            clcf->name.len = name->len - 1;
            clcf->name.data = name->data + 1;
            clcf->exact_match = 1;
        } else if (name->data[0] == '^' && name->data[1] == '~') {
            clcf->name.len = name->len - 2;
            clcf->name.data = name->data + 2;
            clcf->noregex = 1;
        } else if (name->data[0] == '~') {
            name->len--;
            name->data++;
            if (name->data[0] == '*') {
                name->len--;
                name->data++;
                if (ngx_http_core_regex_location(cf, clcf, name, 1) != NGX_OK) {
                    return NGX_CONF_ERROR;
                }
            } else {
                if (ngx_http_core_regex_location(cf, clcf, name, 0) != NGX_OK) {
                    return NGX_CONF_ERROR;
                }
            }

        } else {//指定了一个命名的location。
            clcf->name = *name;
            if (name->data[0] == '@') {
                clcf->named = 1;
            }
        }
    }
    pclcf = pctx->loc_conf[ngx_http_core_module.ctx_index];//得到老的HTTP总core模块的配置。或者说上一个location结构。
    if (pclcf->name.len) {//上一个location结构也有名字。那就说明这里是嵌套的location.
        /* nested location */
#if 0
        clcf->prev_location = pclcf;
#endif
        if (pclcf->exact_match) {//上一个是绝对=等于号匹配的，那不行，非法的。
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,   "location \"%V\" could not be inside the exact location \"%V\"", &clcf->name, &pclcf->name);
            return NGX_CONF_ERROR;
        }
        if (pclcf->named) {//上层location是个命名的location,也不行。
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "location \"%V\" could not be inside the named location \"%V\"", &clcf->name, &pclcf->name);
            return NGX_CONF_ERROR;
        }
        if (clcf->named) {//本节点是个named，也不行，囧
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"named location \"%V\" must be on server level only", &clcf->name);
            return NGX_CONF_ERROR;
        }
        len = pclcf->name.len;
#if (NGX_PCRE)
        if (clcf->regex == NULL && ngx_strncmp(clcf->name.data, pclcf->name.data, len) != 0)
#else
        if (ngx_strncmp(clcf->name.data, pclcf->name.data, len) != 0)
#endif
        {//无论如何，顶层节点必须是内存节点的前缀。在嵌套的时候。
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "location \"%V\" is outside location \"%V\"", &clcf->name, &pclcf->name);
            return NGX_CONF_ERROR;
        }
    }
    if (ngx_http_add_location(cf, &pclcf->locations, clcf) != NGX_OK) {//将我这个location加入到上层的loc里面。注意是上层。
        return NGX_CONF_ERROR;//将clcf放入pclcf->locations的后面，链接到队列后面
    }
    save = *cf;
    cf->ctx = ctx;//替换为我自己的ctx。里面照样有main/srv/loc配置。
    cf->cmd_type = NGX_HTTP_LOC_CONF;
    rv = ngx_conf_parse(cf, NULL);//解析子集
    *cf = save;
    return rv;
}


static ngx_int_t
ngx_http_core_regex_location(ngx_conf_t *cf, ngx_http_core_loc_conf_t *clcf,  ngx_str_t *regex, ngx_uint_t caseless)
{//解析正则表达式，将其放入regex,name字段上面
#if (NGX_PCRE)
    ngx_regex_compile_t  rc;
    u_char               errstr[NGX_MAX_CONF_ERRSTR];
    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
    rc.pattern = *regex;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;
#if (NGX_HAVE_CASELESS_FILESYSTEM)
    rc.options = NGX_REGEX_CASELESS;
#else
    rc.options = caseless;
#endif
    clcf->regex = ngx_http_regex_compile(cf, &rc);
    if (clcf->regex == NULL) {
        return NGX_ERROR;
    }
    clcf->name = *regex;
    return NGX_OK;
#else
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the using of the regex \"%V\" requires PCRE library",  regex);
    return NGX_ERROR;
#endif
}


static char *
ngx_http_core_types(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    char        *rv;
    ngx_conf_t   save;

    if (clcf->types == NULL) {
        clcf->types = ngx_array_create(cf->pool, 64, sizeof(ngx_hash_key_t));
        if (clcf->types == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    save = *cf;
    cf->handler = ngx_http_core_type;
    cf->handler_conf = conf;

    rv = ngx_conf_parse(cf, NULL);

    *cf = save;

    return rv;
}


static char *
ngx_http_core_type(ngx_conf_t *cf, ngx_command_t *dummy, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    ngx_str_t       *value, *content_type, *old, file;
    ngx_uint_t       i, n, hash;
    ngx_hash_key_t  *type;

    value = cf->args->elts;

    if (ngx_strcmp(value[0].data, "include") == 0) {
        file = value[1];

        if (ngx_conf_full_name(cf->cycle, &file, 1) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        return ngx_conf_parse(cf, &file);
    }

    content_type = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    if (content_type == NULL) {
        return NGX_CONF_ERROR;
    }

    *content_type = value[0];

    for (i = 1; i < cf->args->nelts; i++) {

        hash = ngx_hash_strlow(value[i].data, value[i].data, value[i].len);

        type = clcf->types->elts;
        for (n = 0; n < clcf->types->nelts; n++) {
            if (ngx_strcmp(value[i].data, type[n].key.data) == 0) {
                old = type[n].value;
                type[n].value = content_type;

                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate extention \"%V\", "
                                   "content type: \"%V\", "
                                   "old content type: \"%V\"",
                                   &value[i], content_type, old);
                continue;
            }
        }


        type = ngx_array_push(clcf->types);
        if (type == NULL) {
            return NGX_CONF_ERROR;
        }

        type->key = value[i];
        type->key_hash = hash;
        type->value = content_type;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_core_preconfiguration(ngx_conf_t *cf)
{//导入核心的一些变量，在解析配置之前就设置了
    return ngx_http_variables_add_core_vars(cf);
}


static void *
ngx_http_core_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_core_main_conf_t));
    if (cmcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&cmcf->servers, cf->pool, 4,
                       sizeof(ngx_http_core_srv_conf_t *))
        != NGX_OK)
    {
        return NULL;
    }

    cmcf->server_names_hash_max_size = NGX_CONF_UNSET_UINT;
    cmcf->server_names_hash_bucket_size = NGX_CONF_UNSET_UINT;

    cmcf->variables_hash_max_size = NGX_CONF_UNSET_UINT;
    cmcf->variables_hash_bucket_size = NGX_CONF_UNSET_UINT;

    return cmcf;
}


static char *
ngx_http_core_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_core_main_conf_t *cmcf = conf;

    if (cmcf->server_names_hash_max_size == NGX_CONF_UNSET_UINT) {
        cmcf->server_names_hash_max_size = 512;
    }

    if (cmcf->server_names_hash_bucket_size == NGX_CONF_UNSET_UINT) {
        cmcf->server_names_hash_bucket_size = ngx_cacheline_size;
    }

    cmcf->server_names_hash_bucket_size =
            ngx_align(cmcf->server_names_hash_bucket_size, ngx_cacheline_size);


    if (cmcf->variables_hash_max_size == NGX_CONF_UNSET_UINT) {
        cmcf->variables_hash_max_size = 512;
    }

    if (cmcf->variables_hash_bucket_size == NGX_CONF_UNSET_UINT) {
        cmcf->variables_hash_bucket_size = 64;
    }

    cmcf->variables_hash_bucket_size =
               ngx_align(cmcf->variables_hash_bucket_size, ngx_cacheline_size);

    if (cmcf->ncaptures) {
        cmcf->ncaptures = (cmcf->ncaptures + 1) * 3;
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_core_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_core_srv_conf_t  *cscf;

    cscf = ngx_pcalloc(cf->pool, sizeof(ngx_http_core_srv_conf_t));
    if (cscf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->client_large_buffers.num = 0;
     */

    if (ngx_array_init(&cscf->server_names, cf->temp_pool, 4,
                       sizeof(ngx_http_server_name_t))
        != NGX_OK)
    {
        return NULL;
    }

    cscf->connection_pool_size = NGX_CONF_UNSET_SIZE;
    cscf->request_pool_size = NGX_CONF_UNSET_SIZE;
    cscf->client_header_timeout = NGX_CONF_UNSET_MSEC;
    cscf->client_header_buffer_size = NGX_CONF_UNSET_SIZE;
    cscf->ignore_invalid_headers = NGX_CONF_UNSET;
    cscf->merge_slashes = NGX_CONF_UNSET;
    cscf->underscores_in_headers = NGX_CONF_UNSET;

    return cscf;
}


static char *
ngx_http_core_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_core_srv_conf_t *prev = parent;
    ngx_http_core_srv_conf_t *conf = child;

    ngx_http_server_name_t  *sn;

    /* TODO: it does not merge, it inits only */

    ngx_conf_merge_size_value(conf->connection_pool_size,
                              prev->connection_pool_size, 256);
    ngx_conf_merge_size_value(conf->request_pool_size,
                              prev->request_pool_size, 4096);
    ngx_conf_merge_msec_value(conf->client_header_timeout,
                              prev->client_header_timeout, 60000);
    ngx_conf_merge_size_value(conf->client_header_buffer_size,
                              prev->client_header_buffer_size, 1024);
    ngx_conf_merge_bufs_value(conf->large_client_header_buffers,
                              prev->large_client_header_buffers,
                              4, 8192);

    if (conf->large_client_header_buffers.size < conf->connection_pool_size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the \"large_client_header_buffers\" size must be "
                           "equal to or bigger than \"connection_pool_size\"");
        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_value(conf->ignore_invalid_headers,
                              prev->ignore_invalid_headers, 1);

    ngx_conf_merge_value(conf->merge_slashes, prev->merge_slashes, 1);

    ngx_conf_merge_value(conf->underscores_in_headers,
                              prev->underscores_in_headers, 0);

    if (conf->server_name.data == NULL) {
        ngx_str_set(&conf->server_name, "");

        sn = ngx_array_push(&conf->server_names);
        if (sn == NULL) {
            return NGX_CONF_ERROR;
        }

#if (NGX_PCRE)
        sn->regex = NULL;
#endif
        sn->server = conf;
        ngx_str_set(&sn->name, "");
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_core_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_core_loc_conf_t));
    if (clcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     clcf->root = { 0, NULL };
     *     clcf->limit_except = 0;
     *     clcf->post_action = { 0, NULL };
     *     clcf->types = NULL;
     *     clcf->default_type = { 0, NULL };
     *     clcf->error_log = NULL;
     *     clcf->error_pages = NULL;
     *     clcf->try_files = NULL;
     *     clcf->client_body_path = NULL;
     *     clcf->regex = NULL;
     *     clcf->exact_match = 0;
     *     clcf->auto_redirect = 0;
     *     clcf->alias = 0;
     *     clcf->gzip_proxied = 0;
     */

    clcf->client_max_body_size = NGX_CONF_UNSET;
    clcf->client_body_buffer_size = NGX_CONF_UNSET_SIZE;
    clcf->client_body_timeout = NGX_CONF_UNSET_MSEC;
    clcf->satisfy = NGX_CONF_UNSET_UINT;
    clcf->if_modified_since = NGX_CONF_UNSET_UINT;
    clcf->client_body_in_file_only = NGX_CONF_UNSET_UINT;
    clcf->client_body_in_single_buffer = NGX_CONF_UNSET;
    clcf->internal = NGX_CONF_UNSET;
    clcf->sendfile = NGX_CONF_UNSET;
    clcf->sendfile_max_chunk = NGX_CONF_UNSET_SIZE;
#if (NGX_HAVE_FILE_AIO)
    clcf->aio = NGX_CONF_UNSET;
#endif
    clcf->read_ahead = NGX_CONF_UNSET_SIZE;
    clcf->directio = NGX_CONF_UNSET;
    clcf->directio_alignment = NGX_CONF_UNSET;
    clcf->tcp_nopush = NGX_CONF_UNSET;
    clcf->tcp_nodelay = NGX_CONF_UNSET;
    clcf->send_timeout = NGX_CONF_UNSET_MSEC;
    clcf->send_lowat = NGX_CONF_UNSET_SIZE;
    clcf->postpone_output = NGX_CONF_UNSET_SIZE;
    clcf->limit_rate = NGX_CONF_UNSET_SIZE;
    clcf->limit_rate_after = NGX_CONF_UNSET_SIZE;
    clcf->keepalive_timeout = NGX_CONF_UNSET_MSEC;
    clcf->keepalive_header = NGX_CONF_UNSET;
    clcf->keepalive_requests = NGX_CONF_UNSET_UINT;
    clcf->lingering_time = NGX_CONF_UNSET_MSEC;
    clcf->lingering_timeout = NGX_CONF_UNSET_MSEC;
    clcf->resolver_timeout = NGX_CONF_UNSET_MSEC;
    clcf->reset_timedout_connection = NGX_CONF_UNSET;
    clcf->server_name_in_redirect = NGX_CONF_UNSET;
    clcf->port_in_redirect = NGX_CONF_UNSET;
    clcf->msie_padding = NGX_CONF_UNSET;
    clcf->msie_refresh = NGX_CONF_UNSET;
    clcf->log_not_found = NGX_CONF_UNSET;
    clcf->log_subrequest = NGX_CONF_UNSET;
    clcf->recursive_error_pages = NGX_CONF_UNSET;
    clcf->server_tokens = NGX_CONF_UNSET;
    clcf->chunked_transfer_encoding = NGX_CONF_UNSET;
    clcf->types_hash_max_size = NGX_CONF_UNSET_UINT;
    clcf->types_hash_bucket_size = NGX_CONF_UNSET_UINT;

    clcf->open_file_cache = NGX_CONF_UNSET_PTR;
    clcf->open_file_cache_valid = NGX_CONF_UNSET;
    clcf->open_file_cache_min_uses = NGX_CONF_UNSET_UINT;
    clcf->open_file_cache_errors = NGX_CONF_UNSET;
    clcf->open_file_cache_events = NGX_CONF_UNSET;

#if (NGX_HTTP_GZIP)
    clcf->gzip_vary = NGX_CONF_UNSET;
    clcf->gzip_http_version = NGX_CONF_UNSET_UINT;
#if (NGX_PCRE)
    clcf->gzip_disable = NGX_CONF_UNSET_PTR;
#endif
    clcf->gzip_disable_msie6 = 3;
#if (NGX_HTTP_DEGRADATION)
    clcf->gzip_disable_degradation = 3;
#endif
#endif

    return clcf;
}


static ngx_str_t  ngx_http_core_text_html_type = ngx_string("text/html");
static ngx_str_t  ngx_http_core_image_gif_type = ngx_string("image/gif");
static ngx_str_t  ngx_http_core_image_jpeg_type = ngx_string("image/jpeg");

static ngx_hash_key_t  ngx_http_core_default_types[] = {
    { ngx_string("html"), 0, &ngx_http_core_text_html_type },
    { ngx_string("gif"), 0, &ngx_http_core_image_gif_type },
    { ngx_string("jpg"), 0, &ngx_http_core_image_jpeg_type },
    { ngx_null_string, 0, NULL }
};


static char *
ngx_http_core_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_core_loc_conf_t *prev = parent;
    ngx_http_core_loc_conf_t *conf = child;

    ngx_uint_t        i;
    ngx_hash_key_t   *type;
    ngx_hash_init_t   types_hash;

    if (conf->root.data == NULL) {

        conf->alias = prev->alias;
        conf->root = prev->root;
        conf->root_lengths = prev->root_lengths;
        conf->root_values = prev->root_values;

        if (prev->root.data == NULL) {
            ngx_str_set(&conf->root, "html");

            if (ngx_conf_full_name(cf->cycle, &conf->root, 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    if (conf->post_action.data == NULL) {
        conf->post_action = prev->post_action;
    }

    ngx_conf_merge_uint_value(conf->types_hash_max_size,
                              prev->types_hash_max_size, 1024);

    ngx_conf_merge_uint_value(conf->types_hash_bucket_size,
                              prev->types_hash_bucket_size,
                              ngx_cacheline_size);

    conf->types_hash_bucket_size = ngx_align(conf->types_hash_bucket_size,
                                             ngx_cacheline_size);

    /*
     * the special handling the "types" directive in the "http" section
     * to inherit the http's conf->types_hash to all servers
     */

    if (prev->types && prev->types_hash.buckets == NULL) {

        types_hash.hash = &prev->types_hash;
        types_hash.key = ngx_hash_key_lc;
        types_hash.max_size = conf->types_hash_max_size;
        types_hash.bucket_size = conf->types_hash_bucket_size;
        types_hash.name = "types_hash";
        types_hash.pool = cf->pool;
        types_hash.temp_pool = NULL;

        if (ngx_hash_init(&types_hash, prev->types->elts, prev->types->nelts)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    if (conf->types == NULL) {
        conf->types = prev->types;
        conf->types_hash = prev->types_hash;
    }

    if (conf->types == NULL) {
        conf->types = ngx_array_create(cf->pool, 4, sizeof(ngx_hash_key_t));
        if (conf->types == NULL) {
            return NGX_CONF_ERROR;
        }

        for (i = 0; ngx_http_core_default_types[i].key.len; i++) {
            type = ngx_array_push(conf->types);
            if (type == NULL) {
                return NGX_CONF_ERROR;
            }

            type->key = ngx_http_core_default_types[i].key;
            type->key_hash = ngx_hash_key_lc(ngx_http_core_default_types[i].key.data, ngx_http_core_default_types[i].key.len);
            type->value = ngx_http_core_default_types[i].value;
        }
    }

    if (conf->types_hash.buckets == NULL) {

        types_hash.hash = &conf->types_hash;
        types_hash.key = ngx_hash_key_lc;
        types_hash.max_size = conf->types_hash_max_size;
        types_hash.bucket_size = conf->types_hash_bucket_size;
        types_hash.name = "mime_types_hash";
        types_hash.pool = cf->pool;
        types_hash.temp_pool = NULL;

        if (ngx_hash_init(&types_hash, conf->types->elts, conf->types->nelts)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    if (conf->error_log == NULL) {
        if (prev->error_log) {
            conf->error_log = prev->error_log;
        } else {
            conf->error_log = &cf->cycle->new_log;
        }
    }

    if (conf->error_pages == NULL && prev->error_pages) {
        conf->error_pages = prev->error_pages;
    }

    ngx_conf_merge_str_value(conf->default_type,
                              prev->default_type, "text/plain");

    ngx_conf_merge_off_value(conf->client_max_body_size,
                              prev->client_max_body_size, 1 * 1024 * 1024);
    ngx_conf_merge_size_value(conf->client_body_buffer_size,
                              prev->client_body_buffer_size,
                              (size_t) 2 * ngx_pagesize);
    ngx_conf_merge_msec_value(conf->client_body_timeout,
                              prev->client_body_timeout, 60000);

    ngx_conf_merge_uint_value(conf->satisfy, prev->satisfy,
                              NGX_HTTP_SATISFY_ALL);
    ngx_conf_merge_uint_value(conf->if_modified_since, prev->if_modified_since,
                              NGX_HTTP_IMS_EXACT);
    ngx_conf_merge_uint_value(conf->client_body_in_file_only,
                              prev->client_body_in_file_only, 0);
    ngx_conf_merge_value(conf->client_body_in_single_buffer,
                              prev->client_body_in_single_buffer, 0);
    ngx_conf_merge_value(conf->internal, prev->internal, 0);
    ngx_conf_merge_value(conf->sendfile, prev->sendfile, 0);
    ngx_conf_merge_size_value(conf->sendfile_max_chunk,
                              prev->sendfile_max_chunk, 0);
#if (NGX_HAVE_FILE_AIO)
    ngx_conf_merge_value(conf->aio, prev->aio, 0);
#endif
    ngx_conf_merge_size_value(conf->read_ahead, prev->read_ahead, 0);
    ngx_conf_merge_off_value(conf->directio, prev->directio,
                              NGX_MAX_OFF_T_VALUE);
    ngx_conf_merge_off_value(conf->directio_alignment, prev->directio_alignment,
                              512);
    ngx_conf_merge_value(conf->tcp_nopush, prev->tcp_nopush, 0);
    ngx_conf_merge_value(conf->tcp_nodelay, prev->tcp_nodelay, 1);

    ngx_conf_merge_msec_value(conf->send_timeout, prev->send_timeout, 60000);
    ngx_conf_merge_size_value(conf->send_lowat, prev->send_lowat, 0);
    ngx_conf_merge_size_value(conf->postpone_output, prev->postpone_output,
                              1460);
    ngx_conf_merge_size_value(conf->limit_rate, prev->limit_rate, 0);
    ngx_conf_merge_size_value(conf->limit_rate_after, prev->limit_rate_after,
                              0);
    ngx_conf_merge_msec_value(conf->keepalive_timeout,
                              prev->keepalive_timeout, 75000);
    ngx_conf_merge_sec_value(conf->keepalive_header,
                              prev->keepalive_header, 0);
    ngx_conf_merge_uint_value(conf->keepalive_requests,
                              prev->keepalive_requests, 100);
    ngx_conf_merge_msec_value(conf->lingering_time,
                              prev->lingering_time, 30000);
    ngx_conf_merge_msec_value(conf->lingering_timeout,
                              prev->lingering_timeout, 5000);
    ngx_conf_merge_msec_value(conf->resolver_timeout,
                              prev->resolver_timeout, 30000);

    if (conf->resolver == NULL) {

        if (prev->resolver == NULL) {

            /*
             * create dummy resolver in http {} context
             * to inherit it in all servers
             */

            prev->resolver = ngx_resolver_create(cf, NULL);
            if (prev->resolver == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        conf->resolver = prev->resolver;
    }

    if (ngx_conf_merge_path_value(cf, &conf->client_body_temp_path,
                              prev->client_body_temp_path,
                              &ngx_http_client_temp_path)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_value(conf->reset_timedout_connection,
                              prev->reset_timedout_connection, 0);
    ngx_conf_merge_value(conf->server_name_in_redirect,
                              prev->server_name_in_redirect, 0);
    ngx_conf_merge_value(conf->port_in_redirect, prev->port_in_redirect, 1);
    ngx_conf_merge_value(conf->msie_padding, prev->msie_padding, 1);
    ngx_conf_merge_value(conf->msie_refresh, prev->msie_refresh, 0);
    ngx_conf_merge_value(conf->log_not_found, prev->log_not_found, 1);
    ngx_conf_merge_value(conf->log_subrequest, prev->log_subrequest, 0);
    ngx_conf_merge_value(conf->recursive_error_pages,
                              prev->recursive_error_pages, 0);
    ngx_conf_merge_value(conf->server_tokens, prev->server_tokens, 1);
    ngx_conf_merge_value(conf->chunked_transfer_encoding,
                              prev->chunked_transfer_encoding, 1);

    ngx_conf_merge_ptr_value(conf->open_file_cache,
                              prev->open_file_cache, NULL);

    ngx_conf_merge_sec_value(conf->open_file_cache_valid,
                              prev->open_file_cache_valid, 60);

    ngx_conf_merge_uint_value(conf->open_file_cache_min_uses,
                              prev->open_file_cache_min_uses, 1);

    ngx_conf_merge_sec_value(conf->open_file_cache_errors,
                              prev->open_file_cache_errors, 0);

    ngx_conf_merge_sec_value(conf->open_file_cache_events,
                              prev->open_file_cache_events, 0);
#if (NGX_HTTP_GZIP)

    ngx_conf_merge_value(conf->gzip_vary, prev->gzip_vary, 0);
    ngx_conf_merge_uint_value(conf->gzip_http_version, prev->gzip_http_version,
                              NGX_HTTP_VERSION_11);
    ngx_conf_merge_bitmask_value(conf->gzip_proxied, prev->gzip_proxied,
                              (NGX_CONF_BITMASK_SET|NGX_HTTP_GZIP_PROXIED_OFF));

#if (NGX_PCRE)
    ngx_conf_merge_ptr_value(conf->gzip_disable, prev->gzip_disable, NULL);
#endif

    if (conf->gzip_disable_msie6 == 3) {
        conf->gzip_disable_msie6 =
            (prev->gzip_disable_msie6 == 3) ? 0 : prev->gzip_disable_msie6;
    }

#if (NGX_HTTP_DEGRADATION)

    if (conf->gzip_disable_degradation == 3) {
        conf->gzip_disable_degradation =
            (prev->gzip_disable_degradation == 3) ?
                 0 : prev->gzip_disable_degradation;
    }

#endif
#endif

    return NGX_CONF_OK;
}


static char *
ngx_http_core_listen(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//解析到"listen"的时候调用。
    ngx_http_core_srv_conf_t *cscf = conf;

    ngx_str_t              *value, size;
    ngx_url_t               u;
    ngx_uint_t              n;
    ngx_http_listen_opt_t   lsopt;

    cscf->listen = 1;//标记一下，这里有listen端口。
    value = cf->args->elts;
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = value[1];
    u.listen = 1;
    u.default_port = 80;

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%s in \"%V\" of the \"listen\" directive",  u.err, &u.url);
        }
        return NGX_CONF_ERROR;
    }
    ngx_memzero(&lsopt, sizeof(ngx_http_listen_opt_t));
    ngx_memcpy(&lsopt.u.sockaddr, u.sockaddr, u.socklen);
    lsopt.socklen = u.socklen;
    lsopt.backlog = NGX_LISTEN_BACKLOG;
    lsopt.rcvbuf = -1;
    lsopt.sndbuf = -1;
#if (NGX_HAVE_SETFIB)
    lsopt.setfib = -1;
#endif
    lsopt.wildcard = u.wildcard;

    (void) ngx_sock_ntop(&lsopt.u.sockaddr, lsopt.addr,
                         NGX_SOCKADDR_STRLEN, 1);

    for (n = 2; n < cf->args->nelts; n++) {

        if (ngx_strcmp(value[n].data, "default_server") == 0
            || ngx_strcmp(value[n].data, "default") == 0)
        {
            lsopt.default_server = 1;
            continue;
        }

        if (ngx_strcmp(value[n].data, "bind") == 0) {
            lsopt.set = 1;
            lsopt.bind = 1;
            continue;
        }

#if (NGX_HAVE_SETFIB)
        if (ngx_strncmp(value[n].data, "setfib=", 7) == 0) {
            lsopt.setfib = ngx_atoi(value[n].data + 7, value[n].len - 7);

            if (lsopt.setfib == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid setfib \"%V\"", &value[n]);
                return NGX_CONF_ERROR;
            }

            continue;
        }
#endif
        if (ngx_strncmp(value[n].data, "backlog=", 8) == 0) {
            lsopt.backlog = ngx_atoi(value[n].data + 8, value[n].len - 8);
            lsopt.set = 1;
            lsopt.bind = 1;

            if (lsopt.backlog == NGX_ERROR || lsopt.backlog == 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid backlog \"%V\"", &value[n]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[n].data, "rcvbuf=", 7) == 0) {
            size.len = value[n].len - 7;
            size.data = value[n].data + 7;

            lsopt.rcvbuf = ngx_parse_size(&size);
            lsopt.set = 1;
            lsopt.bind = 1;

            if (lsopt.rcvbuf == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid rcvbuf \"%V\"", &value[n]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[n].data, "sndbuf=", 7) == 0) {
            size.len = value[n].len - 7;
            size.data = value[n].data + 7;

            lsopt.sndbuf = ngx_parse_size(&size);
            lsopt.set = 1;
            lsopt.bind = 1;

            if (lsopt.sndbuf == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid sndbuf \"%V\"", &value[n]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[n].data, "accept_filter=", 14) == 0) {
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
            lsopt.accept_filter = (char *) &value[n].data[14];
            lsopt.set = 1;
            lsopt.bind = 1;
#else
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "accept filters \"%V\" are not supported "
                               "on this platform, ignored",
                               &value[n]);
#endif
            continue;
        }

        if (ngx_strcmp(value[n].data, "deferred") == 0) {
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
            lsopt.deferred_accept = 1;
            lsopt.set = 1;
            lsopt.bind = 1;
#else
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "the deferred accept is not supported "
                               "on this platform, ignored");
#endif
            continue;
        }

        if (ngx_strncmp(value[n].data, "ipv6only=o", 10) == 0) {
#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
            struct sockaddr  *sa;

            sa = &lsopt.u.sockaddr;

            if (sa->sa_family == AF_INET6) {

                if (ngx_strcmp(&value[n].data[10], "n") == 0) {
                    lsopt.ipv6only = 1;

                } else if (ngx_strcmp(&value[n].data[10], "ff") == 0) {
                    lsopt.ipv6only = 2;

                } else {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "invalid ipv6only flags \"%s\"",
                                       &value[n].data[9]);
                    return NGX_CONF_ERROR;
                }

                lsopt.set = 1;
                lsopt.bind = 1;

            } else {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "ipv6only is not supported "
                                   "on addr \"%s\", ignored", lsopt.addr);
            }

            continue;
#else
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "bind ipv6only is not supported "
                               "on this platform");
            return NGX_CONF_ERROR;
#endif
        }

        if (ngx_strcmp(value[n].data, "ssl") == 0) {
#if (NGX_HTTP_SSL)
            lsopt.ssl = 1;
            continue;
#else
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "the \"ssl\" parameter requires "
                               "ngx_http_ssl_module");
            return NGX_CONF_ERROR;
#endif
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the invalid \"%V\" parameter", &value[n]);
        return NGX_CONF_ERROR;
    }

    if (ngx_http_add_listen(cf, cscf, &lsopt) == NGX_OK) {
        return NGX_CONF_OK;
    }

    return NGX_CONF_ERROR;
}


static char *
ngx_http_core_server_name(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_srv_conf_t *cscf = conf;

    u_char                   ch;
    ngx_str_t               *value, name;
    ngx_uint_t               i;
    ngx_http_server_name_t  *sn;

    value = cf->args->elts;

    ch = value[1].data[0];

    if (cscf->server_name.data == NULL) {
        name = value[1];

        if (ch == '.') {
            name.len--;
            name.data++;
        }

        cscf->server_name.len = name.len;
        cscf->server_name.data = ngx_pstrdup(cf->pool, &name);
        if (cscf->server_name.data == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    for (i = 1; i < cf->args->nelts; i++) {

        ch = value[i].data[0];

        if ((ch == '*' && (value[i].len < 3 || value[i].data[1] != '.'))
            || (ch == '.' && value[i].len < 2))
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "server name \"%V\" is invalid", &value[i]);
            return NGX_CONF_ERROR;
        }

        if (ngx_strchr(value[i].data, '/')) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "server name \"%V\" has strange symbols",
                               &value[i]);
        }

        if (value[i].len == 1 && ch == '*') {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"server_name *\" is unsupported, use "
                               "\"server_name_in_redirect off\" instead");
            return NGX_CONF_ERROR;
        }

        sn = ngx_array_push(&cscf->server_names);
        if (sn == NULL) {
            return NGX_CONF_ERROR;
        }

#if (NGX_PCRE)
        sn->regex = NULL;
#endif
        sn->server = cscf;
        sn->name = value[i];

        if (value[i].data[0] != '~') {
            ngx_strlow(sn->name.data, sn->name.data, sn->name.len);
            continue;
        }

#if (NGX_PCRE)
        {
        u_char               *p;
        ngx_regex_compile_t   rc;
        u_char                errstr[NGX_MAX_CONF_ERRSTR];

        if (value[i].len == 1) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "empty regex in server name \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }

        value[i].len--;
        value[i].data++;

        ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

        rc.pattern = value[i];
        rc.err.len = NGX_MAX_CONF_ERRSTR;
        rc.err.data = errstr;

        for (p = value[i].data; p < value[i].data + value[i].len; p++) {
            if (*p >= 'A' && *p <= 'Z') {
                rc.options = NGX_REGEX_CASELESS;
                break;
            }
        }

        sn->regex = ngx_http_regex_compile(cf, &rc);
        if (sn->regex == NULL) {
            return NGX_CONF_ERROR;
        }

        sn->name = value[i];
        cscf->captures = (rc.captures > 0);
        }
#else
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the using of the regex \"%V\" "
                           "requires PCRE library", &value[i]);

        return NGX_CONF_ERROR;
#endif
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_core_root(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    ngx_str_t                  *value;
    ngx_int_t                   alias;
    ngx_uint_t                  n;
    ngx_http_script_compile_t   sc;

    alias = (cmd->name.len == sizeof("alias") - 1) ? 1 : 0;

    if (clcf->root.data) {

        if ((clcf->alias != 0) == alias) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"%V\" directive is duplicate",
                               &cmd->name);
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"%V\" directive is duplicate, "
                               "\"%s\" directive is specified before",
                               &cmd->name, clcf->alias ? "alias" : "root");
        }

        return NGX_CONF_ERROR;
    }

    if (clcf->named && alias) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the \"alias\" directive may not be used "
                           "inside named location");

        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    if (ngx_strstr(value[1].data, "$document_root")
        || ngx_strstr(value[1].data, "${document_root}"))
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the $document_root variable may not be used "
                           "in the \"%V\" directive",
                           &cmd->name);

        return NGX_CONF_ERROR;
    }

    if (ngx_strstr(value[1].data, "$realpath_root")
        || ngx_strstr(value[1].data, "${realpath_root}"))
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the $realpath_root variable may not be used "
                           "in the \"%V\" directive",
                           &cmd->name);

        return NGX_CONF_ERROR;
    }

    clcf->alias = alias ? clcf->name.len : 0;
    clcf->root = value[1];

    if (!alias && clcf->root.data[clcf->root.len - 1] == '/') {
        clcf->root.len--;
    }

    if (clcf->root.data[0] != '$') {
        if (ngx_conf_full_name(cf->cycle, &clcf->root, 0) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    n = ngx_http_script_variables_count(&clcf->root);

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.variables = n;

#if (NGX_PCRE)
    if (alias && clcf->regex) {
        n = 1;
    }
#endif

    if (n) {
        sc.cf = cf;
        sc.source = &clcf->root;
        sc.lengths = &clcf->root_lengths;
        sc.values = &clcf->root_values;
        sc.complete_lengths = 1;
        sc.complete_values = 1;

        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static ngx_http_method_name_t  ngx_methods_names[] = {
   { (u_char *) "GET",       (uint32_t) ~NGX_HTTP_GET },
   { (u_char *) "HEAD",      (uint32_t) ~NGX_HTTP_HEAD },
   { (u_char *) "POST",      (uint32_t) ~NGX_HTTP_POST },
   { (u_char *) "PUT",       (uint32_t) ~NGX_HTTP_PUT },
   { (u_char *) "DELETE",    (uint32_t) ~NGX_HTTP_DELETE },
   { (u_char *) "MKCOL",     (uint32_t) ~NGX_HTTP_MKCOL },
   { (u_char *) "COPY",      (uint32_t) ~NGX_HTTP_COPY },
   { (u_char *) "MOVE",      (uint32_t) ~NGX_HTTP_MOVE },
   { (u_char *) "OPTIONS",   (uint32_t) ~NGX_HTTP_OPTIONS },
   { (u_char *) "PROPFIND" , (uint32_t) ~NGX_HTTP_PROPFIND },
   { (u_char *) "PROPPATCH", (uint32_t) ~NGX_HTTP_PROPPATCH },
   { (u_char *) "LOCK",      (uint32_t) ~NGX_HTTP_LOCK },
   { (u_char *) "UNLOCK",    (uint32_t) ~NGX_HTTP_UNLOCK },
   { (u_char *) "PATCH",     (uint32_t) ~NGX_HTTP_PATCH },
   { NULL, 0 }
};


static char *
ngx_http_core_limit_except(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *pclcf = conf;

    char                      *rv;
    void                      *mconf;
    ngx_str_t                 *value;
    ngx_uint_t                 i;
    ngx_conf_t                 save;
    ngx_http_module_t         *module;
    ngx_http_conf_ctx_t       *ctx, *pctx;
    ngx_http_method_name_t    *name;
    ngx_http_core_loc_conf_t  *clcf;

    if (pclcf->limit_except) {
        return "duplicate";
    }

    pclcf->limit_except = 0xffffffff;

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {
        for (name = ngx_methods_names; name->name; name++) {

            if (ngx_strcasecmp(value[i].data, name->name) == 0) {
                pclcf->limit_except &= name->method;
                goto next;
            }
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid method \"%V\"", &value[i]);
        return NGX_CONF_ERROR;

    next:
        continue;
    }

    if (!(pclcf->limit_except & NGX_HTTP_GET)) {
        pclcf->limit_except &= (uint32_t) ~NGX_HTTP_HEAD;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    pctx = cf->ctx;
    ctx->main_conf = pctx->main_conf;
    ctx->srv_conf = pctx->srv_conf;

    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = ngx_modules[i]->ctx;

        if (module->create_loc_conf) {

            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                 return NGX_CONF_ERROR;
            }

            ctx->loc_conf[ngx_modules[i]->ctx_index] = mconf;
        }
    }


    clcf = ctx->loc_conf[ngx_http_core_module.ctx_index];
    pclcf->limit_except_loc_conf = ctx->loc_conf;
    clcf->loc_conf = ctx->loc_conf;
    clcf->name = pclcf->name;
    clcf->noname = 1;
    clcf->lmt_excpt = 1;

    if (ngx_http_add_location(cf, &pclcf->locations, clcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    save = *cf;
    cf->ctx = ctx;
    cf->cmd_type = NGX_HTTP_LMT_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = save;

    return rv;
}


static char *
ngx_http_core_directio(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    ngx_str_t  *value;

    if (clcf->directio != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        clcf->directio = NGX_OPEN_FILE_DIRECTIO_OFF;
        return NGX_CONF_OK;
    }

    clcf->directio = ngx_parse_offset(&value[1]);
    if (clcf->directio == (off_t) NGX_ERROR) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_core_error_page(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    u_char                            *p;
    ngx_int_t                          overwrite;
    ngx_str_t                         *value, uri, args;
    ngx_uint_t                         i, n;
    ngx_http_err_page_t               *err;
    ngx_http_complex_value_t           cv;
    ngx_http_compile_complex_value_t   ccv;

    if (clcf->error_pages == NULL) {
        clcf->error_pages = ngx_array_create(cf->pool, 4,
                                             sizeof(ngx_http_err_page_t));
        if (clcf->error_pages == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    value = cf->args->elts;

    i = cf->args->nelts - 2;

    if (value[i].data[0] == '=') {
        if (i == 1) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid value \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }

        if (value[i].len > 1) {
            overwrite = ngx_atoi(&value[i].data[1], value[i].len - 1);

            if (overwrite == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid value \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

        } else {
            overwrite = 0;
        }

        n = 2;

    } else {
        overwrite = -1;
        n = 1;
    }

    uri = value[cf->args->nelts - 1];

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &uri;
    ccv.complex_value = &cv;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ngx_str_null(&args);

    if (cv.lengths == NULL && uri.data[0] == '/') {
        p = (u_char *) ngx_strchr(uri.data, '?');

        if (p) {
            cv.value.len = p - uri.data;
            cv.value.data = uri.data;
            p++;
            args.len = (uri.data + uri.len) - p;
            args.data = p;
        }
    }

    for (i = 1; i < cf->args->nelts - n; i++) {
        err = ngx_array_push(clcf->error_pages);
        if (err == NULL) {
            return NGX_CONF_ERROR;
        }

        err->status = ngx_atoi(value[i].data, value[i].len);

        if (err->status == NGX_ERROR || err->status == 499) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid value \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }

        if (err->status < 300 || err->status > 599) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "value \"%V\" must be between 300 and 599",
                               &value[i]);
            return NGX_CONF_ERROR;
        }

        err->overwrite = overwrite;

        if (overwrite == -1) {
            switch (err->status) {
                case NGX_HTTP_TO_HTTPS:
                case NGX_HTTPS_CERT_ERROR:
                case NGX_HTTPS_NO_CERT:
                    err->overwrite = NGX_HTTP_BAD_REQUEST;
                default:
                    break;
            }
        }

        err->value = cv;
        err->args = args;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_core_try_files(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    ngx_str_t                  *value;
    ngx_int_t                   code;
    ngx_uint_t                  i, n;
    ngx_http_try_file_t        *tf;
    ngx_http_script_compile_t   sc;
    ngx_http_core_main_conf_t  *cmcf;

    if (clcf->try_files) {
        return "is duplicate";
    }

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    cmcf->try_files = 1;

    tf = ngx_pcalloc(cf->pool, cf->args->nelts * sizeof(ngx_http_try_file_t));
    if (tf == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf->try_files = tf;

    value = cf->args->elts;

    for (i = 0; i < cf->args->nelts - 1; i++) {

        tf[i].name = value[i + 1];

        if (tf[i].name.data[tf[i].name.len - 1] == '/') {
            tf[i].test_dir = 1;
            tf[i].name.len--;
            tf[i].name.data[tf[i].name.len] = '\0';
        }

        n = ngx_http_script_variables_count(&tf[i].name);

        if (n) {
            ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

            sc.cf = cf;
            sc.source = &tf[i].name;
            sc.lengths = &tf[i].lengths;
            sc.values = &tf[i].values;
            sc.variables = n;
            sc.complete_lengths = 1;
            sc.complete_values = 1;

            if (ngx_http_script_compile(&sc) != NGX_OK) {
                return NGX_CONF_ERROR;
            }

        } else {
            /* add trailing '\0' to length */
            tf[i].name.len++;
        }
    }

    if (tf[i - 1].name.data[0] == '=') {

        code = ngx_atoi(tf[i - 1].name.data + 1, tf[i - 1].name.len - 2);

        if (code == NGX_ERROR) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid code \"%*s\"",
                               tf[i - 1].name.len - 1, tf[i - 1].name.data);
            return NGX_CONF_ERROR;
        }

        tf[i].code = code;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_core_open_file_cache(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    time_t       inactive;
    ngx_str_t   *value, s;
    ngx_int_t    max;
    ngx_uint_t   i;

    if (clcf->open_file_cache != NGX_CONF_UNSET_PTR) {
        return "is duplicate";
    }

    value = cf->args->elts;

    max = 0;
    inactive = 60;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "max=", 4) == 0) {

            max = ngx_atoi(value[i].data + 4, value[i].len - 4);
            if (max == NGX_ERROR) {
                goto failed;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "inactive=", 9) == 0) {

            s.len = value[i].len - 9;
            s.data = value[i].data + 9;

            inactive = ngx_parse_time(&s, 1);
            if (inactive < 0) {
                goto failed;
            }

            continue;
        }

        if (ngx_strcmp(value[i].data, "off") == 0) {

            clcf->open_file_cache = NULL;

            continue;
        }

    failed:

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid \"open_file_cache\" parameter \"%V\"",
                           &value[i]);
        return NGX_CONF_ERROR;
    }

    if (clcf->open_file_cache == NULL) {
        return NGX_CONF_OK;
    }

    if (max == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"open_file_cache\" must have \"max\" parameter");
        return NGX_CONF_ERROR;
    }

    clcf->open_file_cache = ngx_open_file_cache_init(cf->pool, max, inactive);
    if (clcf->open_file_cache) {
        return NGX_CONF_OK;
    }

    return NGX_CONF_ERROR;
}


static char *
ngx_http_core_error_log(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    ngx_str_t  *value;

    if (clcf->error_log) {
        return "is duplicate";
    }

    value = cf->args->elts;

    clcf->error_log = ngx_log_create(cf->cycle, &value[1]);
    if (clcf->error_log == NULL) {
        return NGX_CONF_ERROR;
    }

    if (cf->args->nelts == 2) {
        clcf->error_log->log_level = NGX_LOG_ERR;
        return NGX_CONF_OK;
    }

    return ngx_log_set_levels(cf, clcf->error_log);
}


static char *
ngx_http_core_keepalive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    ngx_str_t  *value;

    if (clcf->keepalive_timeout != NGX_CONF_UNSET_MSEC) {
        return "is duplicate";
    }

    value = cf->args->elts;

    clcf->keepalive_timeout = ngx_parse_time(&value[1], 0);

    if (clcf->keepalive_timeout == (ngx_msec_t) NGX_ERROR) {
        return "invalid value";
    }

    if (clcf->keepalive_timeout == (ngx_msec_t) NGX_PARSE_LARGE_TIME) {
        return "value must be less than 597 hours";
    }

    if (cf->args->nelts == 2) {
        return NGX_CONF_OK;
    }

    clcf->keepalive_header = ngx_parse_time(&value[2], 1);

    if (clcf->keepalive_header == NGX_ERROR) {
        return "invalid value";
    }

    if (clcf->keepalive_header == NGX_PARSE_LARGE_TIME) {
        return "value must be less than 68 years";
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_core_internal(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = conf;

    if (clcf->internal != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    clcf->internal = 1;

    return NGX_CONF_OK;
}


static char *
ngx_http_core_resolver(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf = conf;

    ngx_url_t   u;
    ngx_str_t  *value;

    if (clcf->resolver) {
        return "is duplicate";
    }

    value = cf->args->elts;

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.host = value[1];
    u.port = 53;

    if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V: %s", &u.host, u.err);
        return NGX_CONF_ERROR;
    }

    clcf->resolver = ngx_resolver_create(cf, &u.addrs[0]);
    if (clcf->resolver == NULL) {
        return NGX_OK;
    }

    return NGX_CONF_OK;
}


#if (NGX_HTTP_GZIP)

static char *
ngx_http_gzip_disable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf = conf;

#if (NGX_PCRE)

    ngx_str_t            *value;
    ngx_uint_t            i;
    ngx_regex_elt_t      *re;
    ngx_regex_compile_t   rc;
    u_char                errstr[NGX_MAX_CONF_ERRSTR];

    if (clcf->gzip_disable == NGX_CONF_UNSET_PTR) {
        clcf->gzip_disable = ngx_array_create(cf->pool, 2,
                                              sizeof(ngx_regex_elt_t));
        if (clcf->gzip_disable == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    value = cf->args->elts;

    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    rc.pool = cf->pool;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strcmp(value[i].data, "msie6") == 0) {
            clcf->gzip_disable_msie6 = 1;
            continue;
        }

#if (NGX_HTTP_DEGRADATION)

        if (ngx_strcmp(value[i].data, "degradation") == 0) {
            clcf->gzip_disable_degradation = 1;
            continue;
        }

#endif

        re = ngx_array_push(clcf->gzip_disable);
        if (re == NULL) {
            return NGX_CONF_ERROR;
        }

        rc.pattern = value[i];
        rc.options = NGX_REGEX_CASELESS;

        if (ngx_regex_compile(&rc) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V", &rc.err);
            return NGX_CONF_ERROR;
        }

        re->regex = rc.regex;
        re->name = value[i].data;
    }

    return NGX_CONF_OK;

#else
    ngx_str_t   *value;
    ngx_uint_t   i;

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {
        if (ngx_strcmp(value[i].data, "msie6") == 0) {
            clcf->gzip_disable_msie6 = 1;
            continue;
        }

#if (NGX_HTTP_DEGRADATION)

        if (ngx_strcmp(value[i].data, "degradation") == 0) {
            clcf->gzip_disable_degradation = 1;
            continue;
        }

#endif

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "without PCRE library \"gzip_disable\" supports "
                           "builtin \"msie6\" and \"degradation\" mask only");

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;

#endif
}

#endif


static char *
ngx_http_core_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;

    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"send_lowat\" is not supported, ignored");

    *np = 0;

#endif

    return NGX_CONF_OK;
}


static char *
ngx_http_core_pool_size(ngx_conf_t *cf, void *post, void *data)
{
    size_t *sp = data;

    if (*sp < NGX_MIN_POOL_SIZE) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the pool size must be no less than %uz",
                           NGX_MIN_POOL_SIZE);
        return NGX_CONF_ERROR;
    }

    if (*sp % NGX_POOL_ALIGNMENT) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the pool size must be a multiple of %uz",
                           NGX_POOL_ALIGNMENT);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
