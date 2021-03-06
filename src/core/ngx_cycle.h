
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_CYCLE_H_INCLUDED_
#define _NGX_CYCLE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef NGX_CYCLE_POOL_SIZE
#define NGX_CYCLE_POOL_SIZE     16384
#endif


#define NGX_DEBUG_POINTS_STOP   1
#define NGX_DEBUG_POINTS_ABORT  2


typedef struct ngx_shm_zone_s  ngx_shm_zone_t;

typedef ngx_int_t (*ngx_shm_zone_init_pt) (ngx_shm_zone_t *zone, void *data);

struct ngx_shm_zone_s {
    void                     *data;//ngx_http_limit_zone_ctx_t等结构。
    ngx_shm_t                 shm;//共享内存数据结构
    ngx_shm_zone_init_pt      init;//ngx_http_limit_req_init_zone等。
    void                     *tag;
};


struct ngx_cycle_s {
    void                  ****conf_ctx;//数组，保存module->create_conf(cycle);返回的指针
    ngx_pool_t               *pool;

    ngx_log_t                *log;//2个log什么区别?
    ngx_log_t                 new_log;

    ngx_connection_t        **files;//files[s] = ngx_connection_t c;数组
    ngx_connection_t         *free_connections;//空闲连接链表
    ngx_uint_t                free_connection_n;//空闲连接的数目

    ngx_array_t               listening;//监听套接字的数组
    ngx_array_t               pathes;
    ngx_list_t                open_files;//这里打开的文件链表
    ngx_list_t                shared_memory;//共享内存结构，limit_req_zon等用这个来记录请求数据。

    ngx_uint_t                connection_n;//配置中的worker_connections
    ngx_uint_t                files_n;

    ngx_connection_t         *connections;//connection_n个，其里面的read,write跟下面的read/write_events一一对应。
    ngx_event_t              *read_events;//读事件，数量等于connection_n。相当于读事件池
    ngx_event_t              *write_events;//写事件，数量等于connection_n

    ngx_cycle_t              *old_cycle;

    ngx_str_t                 conf_file;//K:/home/haiwen/nginx/conf/nginx.conf
    ngx_str_t                 conf_param;
    ngx_str_t                 conf_prefix;//k:conf file's dir . /home/haiwen/nginx/conf/
    ngx_str_t                 prefix;//k:equal /home/haiwen/nginx/ 
    ngx_str_t                 lock_file;
    ngx_str_t                 hostname;
};


typedef struct {
     ngx_flag_t               daemon;
     ngx_flag_t               master;

     ngx_msec_t               timer_resolution;

     ngx_int_t                worker_processes;//工作进程数量
     ngx_int_t                debug_points;

     ngx_int_t                rlimit_nofile;
     ngx_int_t                rlimit_sigpending;
     off_t                    rlimit_core;

     int                      priority;

     ngx_uint_t               cpu_affinity_n;
     u_long                  *cpu_affinity;

     char                    *username;
     ngx_uid_t                user;
     ngx_gid_t                group;

     ngx_str_t                working_directory;
     ngx_str_t                lock_file;

     ngx_str_t                pid;//pid文件的路径
     ngx_str_t                oldpid;

     ngx_array_t              env;
     char                   **environment;

#if (NGX_THREADS)
     ngx_int_t                worker_threads;
     size_t                   thread_stack_size;
#endif

} ngx_core_conf_t;


typedef struct {
     ngx_pool_t              *pool;   /* pcre's malloc() pool */
} ngx_core_tls_t;


#define ngx_is_init_cycle(cycle)  (cycle->conf_ctx == NULL)


ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle);
ngx_int_t ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log);
void ngx_delete_pidfile(ngx_cycle_t *cycle);
ngx_int_t ngx_signal_process(ngx_cycle_t *cycle, char *sig);
void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user);
char **ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last);
ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);
u_long ngx_get_cpu_affinity(ngx_uint_t n);
ngx_shm_zone_t *ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name,
    size_t size, void *tag);


extern volatile ngx_cycle_t  *ngx_cycle;
extern ngx_array_t            ngx_old_cycles;
extern ngx_module_t           ngx_core_module;
extern ngx_uint_t             ngx_test_config;
extern ngx_uint_t             ngx_quiet_mode;
#if (NGX_THREADS)
extern ngx_tls_key_t          ngx_core_tls_key;
#endif


#endif /* _NGX_CYCLE_H_INCLUDED_ */
