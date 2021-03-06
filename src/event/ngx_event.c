
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define DEFAULT_CONNECTIONS  512


extern ngx_module_t ngx_kqueue_module;
extern ngx_module_t ngx_eventport_module;
extern ngx_module_t ngx_devpoll_module;
extern ngx_module_t ngx_epoll_module;
extern ngx_module_t ngx_rtsig_module;
extern ngx_module_t ngx_select_module;


static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_event_process_init(ngx_cycle_t *cycle);
static char *ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_event_create_conf(ngx_cycle_t *cycle);
static char *ngx_event_init_conf(ngx_cycle_t *cycle, void *conf);


static ngx_uint_t     ngx_timer_resolution;//是否要开启精确时间选项，值是精确度
sig_atomic_t          ngx_event_timer_alarm;//这个是啥意思

static ngx_uint_t     ngx_event_max_module;

ngx_uint_t            ngx_event_flags;
ngx_event_actions_t   ngx_event_actions;//ngx_epoll_init中设置，代表使用的event结构。ngx_epoll_module_ctx.actions;


static ngx_atomic_t   connection_counter = 1;
ngx_atomic_t         *ngx_connection_counter = &connection_counter;


ngx_atomic_t         *ngx_accept_mutex_ptr;
ngx_shmtx_t           ngx_accept_mutex; //监听的锁
ngx_uint_t            ngx_use_accept_mutex;//是否使用上面的锁
//ngx_use_accept_mutex表示是否需要通过对accept加锁来解决惊群问题。当nginx worker进程数>1时且配置文件中打开accept_mutex时，这个标志置为1  
ngx_uint_t            ngx_accept_events;
ngx_uint_t            ngx_accept_mutex_held;//我已经拿到了accept锁，
ngx_msec_t            ngx_accept_mutex_delay;

ngx_int_t             ngx_accept_disabled;
//ngx_accept_disabled表示此时满负荷，没必要再处理新连接了，我们在nginx.conf曾经配置了每一个
//ginx worker进程能够处理的最大连接数，当达到最大数的7/8时，ngx_accept_disabled为正，说明本nginx worker进程非常繁忙，
//将不再去处理新连接，这也是个简单的负载均衡  

ngx_file_t            ngx_accept_mutex_lock_file;


#if (NGX_STAT_STUB)
ngx_atomic_t   ngx_stat_accepted0;
ngx_atomic_t  *ngx_stat_accepted = &ngx_stat_accepted0;
ngx_atomic_t   ngx_stat_handled0;
ngx_atomic_t  *ngx_stat_handled = &ngx_stat_handled0;
ngx_atomic_t   ngx_stat_requests0;
ngx_atomic_t  *ngx_stat_requests = &ngx_stat_requests0;
ngx_atomic_t   ngx_stat_active0;
ngx_atomic_t  *ngx_stat_active = &ngx_stat_active0;
ngx_atomic_t   ngx_stat_reading0;
ngx_atomic_t  *ngx_stat_reading = &ngx_stat_reading0;
ngx_atomic_t   ngx_stat_writing0;
ngx_atomic_t  *ngx_stat_writing = &ngx_stat_writing0;
#endif



static ngx_command_t  ngx_events_commands[] = {//该ngx_events_module模块的所有配置指令如下。
    { ngx_string("events"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_events_block,//指令的set函数,会调用ngx_conf_parse继续解析本块的内容
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_events_module_ctx = {
    ngx_string("events"),//events {}这样的块解析，没有啥东西。
    NULL,//create_conf
    NULL//init_conf回调
};

ngx_module_t  ngx_events_module = {//ngx_modules里面设置的，这个是整个事件的管理模块，里面会初始化各种事件模块比如ngx_event_core_module。
    NGX_MODULE_V1,
    &ngx_events_module_ctx,                /* module context */
    ngx_events_commands,                   /* module directives *///本模块的指令列表
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master *///为空
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};
////////////////////////////////////////////////////////


static ngx_str_t  event_core_name = ngx_string("event_core");
static ngx_command_t  ngx_event_core_commands[] = {

    { ngx_string("worker_connections"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,//只有一个参数
      ngx_event_connections,
      0,
      0,
      NULL },

    { ngx_string("connections"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_connections,
      0,
      0,
      NULL },

    { ngx_string("use"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_use,
      0,
      0,
      NULL },

    { ngx_string("multi_accept"),
      NGX_EVENT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_event_conf_t, multi_accept),
      NULL },

    { ngx_string("accept_mutex"),
      NGX_EVENT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_event_conf_t, accept_mutex),
      NULL },

    { ngx_string("accept_mutex_delay"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_event_conf_t, accept_mutex_delay),
      NULL },

    { ngx_string("debug_connection"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_debug_connection,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_event_module_t  ngx_event_core_module_ctx = {
    &event_core_name,
    ngx_event_create_conf,                 /* create configuration */
    ngx_event_init_conf,                   /* init configuration */

    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};
//ngx_events_module和ngx_event_core_module都有
ngx_module_t  ngx_event_core_module = {
    NGX_MODULE_V1,
    &ngx_event_core_module_ctx,            /* module context */
    ngx_event_core_commands,               /* module directives */
    NGX_EVENT_MODULE,                      /* module type */
    NULL,                                  /* init master */
    ngx_event_module_init,                 /* init module */
    ngx_event_process_init,                /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


void
ngx_process_events_and_timers(ngx_cycle_t *cycle)
{//处理一轮事件和定时器。
    ngx_uint_t  flags;
    ngx_msec_t  timer, delta;
//也就是说，配置文件中使用了timer_resolution指令后，epoll_wait将使用信号中断的机制来驱动定时器，
//否则将使用定时器红黑树的最小时间作为epoll_wait超时时间来驱动定时器。定时器在ngx_event_process_init设置
    if (ngx_timer_resolution) {//如果配置里面设置了时间精确度.
        timer = NGX_TIMER_INFINITE;//因为这里我们因为ngx_timer_resolution。那这里的意思是，就算有定时器快要超时了，但还是要等到定时器触发才行。
//为何不取个最小值，因为没法取最小值，这是折中
        flags = 0;
    } else {//因为没有定时器，所以得用红黑树最小时间
        timer = ngx_event_find_timer();//返回最小的还要多久超时，我这回epoll_wait最长等这么久了，不然晚了
        flags = NGX_UPDATE_TIME;//待会epoll_wait等待之后，需要更新一下时间。
#if (NGX_THREADS)
        if (timer == NGX_TIMER_INFINITE || timer > 500) {
            timer = 500;
        }
#endif
    }
//如果配置需要有accept锁避免进群问题，则先获得所，在获得锁的内部，会将监听句柄放入epoll的。
//改天测试一下
    if (ngx_use_accept_mutex) {//listupdate服务是没有开启的，也就无法避免惊群问题
        if (ngx_accept_disabled > 0) {//控制频率的，大于7/8就开启
            ngx_accept_disabled--;
        } else {//获取锁,并且将监听SOCK放入epoll，一遍这次进行监控，否则不监控新连接
            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR) {
                return;
            }

            if (ngx_accept_mutex_held) {//拿到锁了，待会就得悠着点
                flags |= NGX_POST_EVENTS;
//拿到锁的话，置flag为NGX_POST_EVENTS，这意味着ngx_process_events函数中，
//任何事件都将延后处理，会把accept事件都放到ngx_posted_accept_events链表中，
//epollin|epollout事件都放到ngx_posted_events链表中  。其实就是想尽早释放这个锁，以便给别的进程用
            } else {//如果没有拿到锁
                if (timer == NGX_TIMER_INFINITE || timer > ngx_accept_mutex_delay) {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }//如果没有配置accept_mutex on ， 那么就会有惊群问题出现

    delta = ngx_current_msec;//当前时间
//进行epoll_wait，如果需要accept锁且拿到了，就同时监控listening fd，否则监控可读可写事件，根据需要放入ngx_posted_accept_events链表
    (void) ngx_process_events(cycle, timer, flags);//调用ngx_epoll_process_events

    delta = ngx_current_msec - delta;//处理事件时间差
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "timer delta: %M", delta);

    if (ngx_posted_accept_events) {//如果accept延后处理链表中有数据，那么就先赶紧accept之，然后马上释放锁，让别的进程能访问
        ngx_event_process_posted(cycle, &ngx_posted_accept_events);//ngx_event_accept
    }
    if (ngx_accept_mutex_held) {//刚拿到了锁，如果有新的连接，我已经accept了，解锁
        ngx_shmtx_unlock(&ngx_accept_mutex);
    }

    if (delta) {//妙!如果刚才的ngx_process_events没有花费太久，1秒都没有，那丫的都不用去处理定时器，因为压根没有超时的肯定。牛逼
        ngx_event_expire_timers();//把超时的回调了
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "posted events %p", ngx_posted_events);
    if (ngx_posted_events) {//锁也释放了，现在需要处理一下数据读写事件了
        if (ngx_threaded) {
            ngx_wakeup_worker_thread(cycle);
        } else {
            ngx_event_process_posted(cycle, &ngx_posted_events);//处理挂到队列的读写事件。
        }
    }
}


ngx_int_t
ngx_handle_read_event(ngx_event_t *rev, ngx_uint_t flags)
{//将一个连接加入可读事件监听中。参数为0的话不做任何动作。
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {
        /* kqueue, epoll */
        if (!rev->active && !rev->ready) {//如果不活跃，还没有设置进去，不ready，没有数据可以读，就加入到epoll
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_CLEAR_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }
        return NGX_OK;
    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {
        /* select, poll, /dev/poll */
        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
        if (rev->active && (rev->ready || (flags & NGX_CLOSE_EVENT))) {
            if (ngx_del_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT | flags)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {
        /* event ports */
        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
        if (rev->oneshot && !rev->ready) {
            if (ngx_del_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
    }
    /* aio, iocp, rtsig */
    return NGX_OK;
}


ngx_int_t
ngx_handle_write_event(ngx_event_t *wev, size_t lowat)
{//只是注册了一下读写事件。
    ngx_connection_t  *c;

    if (lowat) {
        c = wev->data;

        if (ngx_send_lowat(c, lowat) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {
        /* kqueue, epoll */
        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_CLEAR_EVENT | (lowat ? NGX_LOWAT_EVENT : 0)) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }
        return NGX_OK;
    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {
        /* select, poll, /dev/poll */
        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
        if (wev->active && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
            return NGX_OK;
        }

    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {
        /* event ports */
        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
        if (wev->oneshot && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
    }
    /* aio, iocp, rtsig */
    return NGX_OK;
}


static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle)
{//ngx_init_cycle函数里面会遍历调用所有模块的模块初始化函数。这是在主函数里面调用的。
//分配所需的共享内存，比如ngx_accept_mutex_ptr等。
    void              ***cf;
    u_char              *shared;
    size_t               size, cl;
    ngx_shm_t            shm;
    ngx_time_t          *tp;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;

    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);//得到在create_conf里面分配的配置数据
    if (cf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no \"events\" section in configuration");
        return NGX_ERROR;
    }

    ecf = (*cf)[ngx_event_core_module.ctx_index];//得到主模块的配置，在其create_conf 也就是ngx_event_create_conf返回的配置数据
    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "using the \"%s\" event method", ecf->name);
    }
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//得到祖师爷的配置

    ngx_timer_resolution = ccf->timer_resolution;//是否要开启精确时间选项，值是精确度

#if !(NGX_WIN32)
    {
    ngx_int_t      limit;
    struct rlimit  rlmt;
    if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,  "getrlimit(RLIMIT_NOFILE) failed, ignored");

    } else {
        if (ecf->connections > (ngx_uint_t) rlmt.rlim_cur && (ccf->rlimit_nofile == NGX_CONF_UNSET
			|| ecf->connections > (ngx_uint_t) ccf->rlimit_nofile)) {
            limit = (ccf->rlimit_nofile == NGX_CONF_UNSET) ? (ngx_int_t) rlmt.rlim_cur : ccf->rlimit_nofile;
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0, "%ui worker_connections are more than open file resource limit: %i", ecf->connections, limit);
        }
    }
	}
#endif /* !(NGX_WIN32) */
    if (ccf->master == 0) {
        return NGX_OK;
    }
    if (ngx_accept_mutex_ptr) {
        return NGX_OK;
    }
    /* cl should be equal or bigger than cache line size */
    cl = 128;
    size = cl            /* ngx_accept_mutex */
           + cl          /* ngx_connection_counter */
           + cl;         /* ngx_temp_number */

#if (NGX_STAT_STUB)
    size += cl           /* ngx_stat_accepted */
           + cl          /* ngx_stat_handled */
           + cl          /* ngx_stat_requests */
           + cl          /* ngx_stat_active */
           + cl          /* ngx_stat_reading */
           + cl;         /* ngx_stat_writing */
#endif
	//为上述数据结构分配共享内存。
    shm.size = size;
    shm.name.len = sizeof("nginx_shared_zone");
    shm.name.data = (u_char *) "nginx_shared_zone";
    shm.log = cycle->log;

    if (ngx_shm_alloc(&shm) != NGX_OK) {
        return NGX_ERROR;
    }
    shared = shm.addr;
    ngx_accept_mutex_ptr = (ngx_atomic_t *) shared;
    if (ngx_shmtx_create(&ngx_accept_mutex, shared, cycle->lock_file.data)
        != NGX_OK)
    {
        return NGX_ERROR;
    }
    ngx_connection_counter = (ngx_atomic_t *) (shared + 1 * cl);
    (void) ngx_atomic_cmp_set(ngx_connection_counter, 0, 1);
    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "counter: %p, %d", ngx_connection_counter, *ngx_connection_counter);
    ngx_temp_number = (ngx_atomic_t *) (shared + 2 * cl);
    tp = ngx_timeofday();
    ngx_random_number = (tp->msec << 16) + ngx_pid;
#if (NGX_STAT_STUB)

    ngx_stat_accepted = (ngx_atomic_t *) (shared + 3 * cl);
    ngx_stat_handled = (ngx_atomic_t *) (shared + 4 * cl);
    ngx_stat_requests = (ngx_atomic_t *) (shared + 5 * cl);
    ngx_stat_active = (ngx_atomic_t *) (shared + 6 * cl);
    ngx_stat_reading = (ngx_atomic_t *) (shared + 7 * cl);
    ngx_stat_writing = (ngx_atomic_t *) (shared + 8 * cl);

#endif

    return NGX_OK;
}


#if !(NGX_WIN32)

void
ngx_timer_signal_handler(int signo)
{//定时器信号处理函数，再次设置后，epoll_wait等待后如果为1，表示定时器到了，需要更新时间
    ngx_event_timer_alarm = 1;

#if 1
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ngx_cycle->log, 0, "timer signal");
#endif
}

#endif


static ngx_int_t
ngx_event_process_init(ngx_cycle_t *cycle)
{//进程初始化后，会调用每个模块的进程初始化函数。
	//设置监听SOCK的handler回调函数，这个函数负责accept，然后加入读写事件等

    ngx_uint_t           m, i;
    ngx_event_t         *rev, *wev;
    ngx_listening_t     *ls;
    ngx_connection_t    *c, *next, *old;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;
    ngx_event_module_t  *module;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//得到祖师爷的配置
    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);//先得到ngx_events_module容器的配置，然后得到core事件结构的配置

    if (ccf->master && ccf->worker_processes > 1 && ecf->accept_mutex) {//如果进程数大于1且配置中accept_mutex非0
        ngx_use_accept_mutex = 1;//要使用accept锁
        ngx_accept_mutex_held = 0;
        ngx_accept_mutex_delay = ecf->accept_mutex_delay;

    } else {
        ngx_use_accept_mutex = 0;
    }

#if (NGX_THREADS)
    ngx_posted_events_mutex = ngx_mutex_init(cycle->log, 0);
    if (ngx_posted_events_mutex == NULL) {
        return NGX_ERROR;
    }
#endif
//初始化红黑树结构
    if (ngx_event_timer_init(cycle->log) == NGX_ERROR) {
        return NGX_ERROR;
    }

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }
        if (ngx_modules[m]->ctx_index != ecf->use) {//找到所使用的那个事件模型。
            continue;//在use里面设置的
        }
        module = ngx_modules[m]->ctx;//如果使用了ngx_epoll_module。那么其ctx为ngx_epoll_module_ctx。里面包含很多回调。
        if (module->actions.init(cycle, ngx_timer_resolution) != NGX_OK) {//调用ngx_epoll_init
            /* fatal */
            exit(2);
        }

        break;
    }

#if !(NGX_WIN32)
//配置了ngx_timer_resolution才会设置定时器，指定时间抽发，这样epoll不用设置超时时间了，因为定时器会触发它返回的
    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT)) {
        struct sigaction  sa;
        struct itimerval  itv;

        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = ngx_timer_signal_handler;
        sigemptyset(&sa.sa_mask);
//注册定时器回调函数为ngx_timer_signal_handler
        if (sigaction(SIGALRM, &sa, NULL) == -1) {//注意用的不是signal，这样不用每次都设置，不会被内核每次重置
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "sigaction(SIGALRM) failed");
            return NGX_ERROR;
        }

        itv.it_interval.tv_sec = ngx_timer_resolution / 1000;
        itv.it_interval.tv_usec = (ngx_timer_resolution % 1000) * 1000;
        itv.it_value.tv_sec = ngx_timer_resolution / 1000;
        itv.it_value.tv_usec = (ngx_timer_resolution % 1000 ) * 1000;
//设置定时器
        if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setitimer() failed");
        }
    }

    if (ngx_event_flags & NGX_USE_FD_EVENT) {
        struct rlimit  rlmt;
        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "getrlimit(RLIMIT_NOFILE) failed");
            return NGX_ERROR;
        }
        cycle->files_n = (ngx_uint_t) rlmt.rlim_cur;
        cycle->files = ngx_calloc(sizeof(ngx_connection_t *) * cycle->files_n,   cycle->log);
        if (cycle->files == NULL) {
            return NGX_ERROR;
        }
    }
#endif
//cycle->connection_n为connections配置项的大小
    cycle->connections = ngx_alloc(sizeof(ngx_connection_t) * cycle->connection_n, cycle->log);
    if (cycle->connections == NULL) {
        return NGX_ERROR;
    }

    c = cycle->connections;
//分配读事件内存
    cycle->read_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n, cycle->log);
    if (cycle->read_events == NULL) {
        return NGX_ERROR;
    }

    rev = cycle->read_events;
    for (i = 0; i < cycle->connection_n; i++) {
        rev[i].closed = 1;
        rev[i].instance = 1;
#if (NGX_THREADS)
        rev[i].lock = &c[i].lock;
        rev[i].own_lock = &c[i].lock;
#endif
    }
//分配写事件内存
    cycle->write_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,cycle->log);
    if (cycle->write_events == NULL) {
        return NGX_ERROR;
    }
    wev = cycle->write_events;
    for (i = 0; i < cycle->connection_n; i++) {
        wev[i].closed = 1;
#if (NGX_THREADS)
        wev[i].lock = &c[i].lock;
        wev[i].own_lock = &c[i].lock;
#endif
    }
    i = cycle->connection_n;
    next = NULL;
    do {
        i--;
        c[i].data = next;//串起来，形成数组,从前往后指
        c[i].read = &cycle->read_events[i];
        c[i].write = &cycle->write_events[i];
        c[i].fd = (ngx_socket_t) -1;
        next = &c[i];

#if (NGX_THREADS)
        c[i].lock = 0;
#endif
    } while (i);

    cycle->free_connections = next;
    cycle->free_connection_n = cycle->connection_n;

    /* for each listening socket */
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ngx_get_connection(ls[i].fd, cycle->log);
        if (c == NULL) {
            return NGX_ERROR;
        }
        c->log = &ls[i].log;
        c->listening = &ls[i];//指向这个监听的链接所属的listing结构
        ls[i].connection = c;//指向其所指的连接。注意一个监听SOCK可能有很多连接指向自己，而它的connection只指向其所指的那个连接，即放入epoll的

        rev = c->read;//实际上，c[i].read = &cycle->read_events[i];连接结构里面的read事件指向cycle->read_events对应项
//也就是说，cycle->read_events是事件池，一个池子。跟连接对应的。
        rev->log = c->log;
        rev->accept = 1;//这个当然是监听fd了

#if (NGX_HAVE_DEFERRED_ACCEPT)
        rev->deferred_accept = ls[i].deferred_accept;
#endif
        if (!(ngx_event_flags & NGX_USE_IOCP_EVENT)) {
            if (ls[i].previous) {
                /*
                 * delete the old accept events that were bound to
                 * the old cycle read events array
                 */
                old = ls[i].previous->connection;
                if (ngx_del_event(old->read, NGX_READ_EVENT, NGX_CLOSE_EVENT)  == NGX_ERROR) {
                    return NGX_ERROR;
                }
                old->fd = (ngx_socket_t) -1;
            }
        }
//设置监听SOCK的事件回调句柄。此函数负责accept
        rev->handler = ngx_event_accept;//设置监听SOCK的handler回调函数，这个函数负责accept，然后加入读写事件等

        if (ngx_use_accept_mutex) {
            continue;//如果有ngx_use_accept_mutex，那么这里先不用加到epoll，因为每次循环，获得锁后会加的，或者会去掉的。
        }

        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_add_conn(c) == NGX_ERROR) {
                return NGX_ERROR;
            }

        } else {//默认先将这个连接加进去再说，待会在判断。因为没有ngx_use_accept_mutex
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

#endif

    }

    return NGX_OK;
}


ngx_int_t
ngx_send_lowat(ngx_connection_t *c, size_t lowat)
{
    int  sndlowat;

#if (NGX_HAVE_LOWAT_EVENT)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        c->write->available = lowat;
        return NGX_OK;
    }

#endif

    if (lowat == 0 || c->sndlowat) {
        return NGX_OK;
    }

    sndlowat = (int) lowat;

    if (setsockopt(c->fd, SOL_SOCKET, SO_SNDLOWAT,
                   (const void *) &sndlowat, sizeof(int))
        == -1)
    {
        ngx_connection_error(c, ngx_socket_errno,
                             "setsockopt(SO_SNDLOWAT) failed");
        return NGX_ERROR;
    }

    c->sndlowat = 1;

    return NGX_OK;
}


static char *
ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//当解析到events {}块的时候，调用本指令的set函数。
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_event_module_t   *m;

    /* count the number of the event modules and set up their indices *///（index的复数）
    ngx_event_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {//比如ngx_event_core_module这个模块
            continue;
        }//对NGX_EVENT_MODULE类型的模块进行编号，设置序号，这个标号为EVENT_MODULE类型的模块的编号，不是全局的
        ngx_modules[i]->ctx_index = ngx_event_max_module++;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));//分配一个上下文指针
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *(void **) conf = ctx;//给上层设置上下文结构体

    for (i = 0; ngx_modules[i]; i++) {//遍历每一个NGX_EVENT_MODULE类型的模块，调用其create_conf回调
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }
        m = ngx_modules[i]->ctx;//
        if (m->create_conf) {//ngx_modules[i]->ctx_index是该模块在该类型的序号。
            (*ctx)[ngx_modules[i]->ctx_index] = m->create_conf(cf->cycle);//返回值几位这个配置
            if ((*ctx)[ngx_modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }
    pcf = *cf;//设置一下上下文，进行递归解析，并保持整个结构体的内容。
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;
    rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;//还原上下文
    if (rv != NGX_CONF_OK)
        return rv;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }
        m = ngx_modules[i]->ctx;
        if (m->init_conf) {//进行模块的初始化。此时已经加载完配置了
            rv = m->init_conf(cf->cycle, (*ctx)[ngx_modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//最大连接数
    ngx_event_conf_t  *ecf = conf;

    ngx_str_t  *value;

    if (ecf->connections != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }
    if (ngx_strcmp(cmd->name.data, "connections") == 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"connections\" directive is deprecated, use the \"worker_connections\" directive instead");
    }

    value = cf->args->elts;//参数数组
    ecf->connections = ngx_atoi(value[1].data, value[1].len);//第一个参数应该是整数，设置到event模块的变量中。
    if (ecf->connections == (ngx_uint_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid number \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    cf->cycle->connection_n = ecf->connections;//同时设置给cycle的connection_n
    return NGX_CONF_OK;
}


static char *
ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//扫一遍所用的模块，根据名字找到模块。然后设置上去。
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t             m;
    ngx_str_t            *value;
    ngx_event_conf_t     *old_ecf;
    ngx_event_module_t   *module;

    if (ecf->use != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    if (cf->cycle->old_cycle->conf_ctx) {
        old_ecf = ngx_event_get_conf(cf->cycle->old_cycle->conf_ctx, ngx_event_core_module);
    } else {
        old_ecf = NULL;
    }

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;//找到所要设置的模块。
        if (module->name->len == value[1].len) {
            if (ngx_strcmp(module->name->data, value[1].data) == 0) {
                ecf->use = ngx_modules[m]->ctx_index;//设置为该模块在所属类型的下标
                ecf->name = module->name->data;//名字

                if (ngx_process == NGX_PROCESS_SINGLE && old_ecf  && old_ecf->use != ecf->use)  {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "when the server runs without a master process "
                               "the \"%V\" event type must be the same as "
                               "in previous configuration - \"%s\" "
                               "and it can not be changed on the fly, "
                               "to change it you need to stop server "
                               "and start it again",
                               &value[1], old_ecf->name);
                    return NGX_CONF_ERROR;
                }
                return NGX_CONF_OK;
            }
        }
    }
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid event type \"%V\"", &value[1]);
    return NGX_CONF_ERROR;
}


static char *
ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_DEBUG)
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t           rc;
    ngx_str_t          *value;
    ngx_event_debug_t  *dc;
    struct hostent     *h;
    ngx_cidr_t          cidr;

    value = cf->args->elts;

    dc = ngx_array_push(&ecf->debug_connection);
    if (dc == NULL) {
        return NGX_CONF_ERROR;
    }

    rc = ngx_ptocidr(&value[1], &cidr);

    if (rc == NGX_DONE) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "low address bits of %V are meaningless", &value[1]);
        rc = NGX_OK;
    }

    if (rc == NGX_OK) {

        /* AF_INET only */

        if (cidr.family != AF_INET) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"debug_connection\" supports IPv4 only");
            return NGX_CONF_ERROR;
        }

        dc->mask = cidr.u.in.mask;
        dc->addr = cidr.u.in.addr;

        return NGX_CONF_OK;
    }

    h = gethostbyname((char *) value[1].data);

    if (h == NULL || h->h_addr_list[0] == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "host \"%s\" not found", value[1].data);
        return NGX_CONF_ERROR;
    }

    dc->mask = 0xffffffff;
    dc->addr = *(in_addr_t *)(h->h_addr_list[0]);

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"debug_connection\" is ignored, you need to rebuild "
                       "nginx using --with-debug option to enable it");

#endif

    return NGX_CONF_OK;
}


static void *
ngx_event_create_conf(ngx_cycle_t *cycle)
{//碰到events{}的时候，会预先调用这里。
    ngx_event_conf_t  *ecf;
    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL) {
        return NULL;
    }
	//分配好了配置的内存，进行初始化
    ecf->connections = NGX_CONF_UNSET_UINT;
    ecf->use = NGX_CONF_UNSET_UINT;
    ecf->multi_accept = NGX_CONF_UNSET;
    ecf->accept_mutex = NGX_CONF_UNSET;
    ecf->accept_mutex_delay = NGX_CONF_UNSET_MSEC;
    ecf->name = (void *) NGX_CONF_UNSET;
#if (NGX_DEBUG)
    if (ngx_array_init(&ecf->debug_connection, cycle->pool, 4, sizeof(ngx_event_debug_t)) == NGX_ERROR)  {
        return NULL;
    }
#endif
    return ecf;//返回这段内存给上层。上层会设置到其ctx数组的
}


static char *
ngx_event_init_conf(ngx_cycle_t *cycle, void *conf)
{//初始化模块，此时已经加载了相关的配置了。这里只是设置了一些初始值
    ngx_event_conf_t  *ecf = conf;//得到我在ngx_event_create_conf里面设置的配置结构
#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    int                  fd;
#endif
#if (NGX_HAVE_RTSIG)
    ngx_uint_t           rtsig;
    ngx_core_conf_t     *ccf;
#endif
    ngx_int_t            i;
    ngx_module_t        *module;
    ngx_event_module_t  *event_module;

//下面判断应该使用哪一个网络模块。设置在module变量上。
    module = NULL;
#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    fd = epoll_create(100);
    if (fd != -1) {//果然有epoll，用之
        close(fd);
        module = &ngx_epoll_module;//配置了要用epoll，就用epoll
    } else if (ngx_errno != NGX_ENOSYS) {
        module = &ngx_epoll_module;
    }
#endif
#if (NGX_HAVE_RTSIG)
    if (module == NULL) {
        module = &ngx_rtsig_module;
        rtsig = 1;
    } else {
        rtsig = 0;
    }
#endif
#if (NGX_HAVE_DEVPOLL)
    module = &ngx_devpoll_module;
#endif
#if (NGX_HAVE_KQUEUE)
    module = &ngx_kqueue_module;
#endif
#if (NGX_HAVE_SELECT)
    if (module == NULL) {
        module = &ngx_select_module;
    }
#endif
//得到了所应该使用的模块
    if (module == NULL) {//如果没有得到。就找第一个名字不等于event_core_name的模块
        for (i = 0; ngx_modules[i]; i++) {
            if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
                continue;
            }
            event_module = ngx_modules[i]->ctx;
            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0) {
                continue;//这里是如果等于event_core，就继续，那么不等于就OK了，就是说找一个就行�
            }
            module = ngx_modules[i];
            break;
        }
    }
    if (module == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }
	
    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);//如果没有设置值，就设置为后面的默认值
    cycle->connection_n = ecf->connections;
    ngx_conf_init_uint_value(ecf->use, module->ctx_index);
    event_module = module->ctx;//得到该模块的上下文，比如ngx_event_module_t ngx_event_core_module_ctx 
    ngx_conf_init_ptr_value(ecf->name, event_module->name->data);

    ngx_conf_init_value(ecf->multi_accept, 0);
    ngx_conf_init_value(ecf->accept_mutex, 1);
    ngx_conf_init_msec_value(ecf->accept_mutex_delay, 500);


#if (NGX_HAVE_RTSIG)
    if (!rtsig) {
        return NGX_CONF_OK;
    }
    if (ecf->accept_mutex) {
        return NGX_CONF_OK;
    }
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    if (ccf->worker_processes == 0) {
        return NGX_CONF_OK;
    }
    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,   "the \"rtsig\" method requires \"accept_mutex\" to be on");
    return NGX_CONF_ERROR;
#else
    return NGX_CONF_OK;

#endif
}
