
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


static ngx_uint_t     ngx_timer_resolution;//�Ƿ�Ҫ������ȷʱ��ѡ�ֵ�Ǿ�ȷ��
sig_atomic_t          ngx_event_timer_alarm;//�����ɶ��˼

static ngx_uint_t     ngx_event_max_module;

ngx_uint_t            ngx_event_flags;
ngx_event_actions_t   ngx_event_actions;//ngx_epoll_init�����ã�����ʹ�õ�event�ṹ��ngx_epoll_module_ctx.actions;


static ngx_atomic_t   connection_counter = 1;
ngx_atomic_t         *ngx_connection_counter = &connection_counter;


ngx_atomic_t         *ngx_accept_mutex_ptr;
ngx_shmtx_t           ngx_accept_mutex; //��������
ngx_uint_t            ngx_use_accept_mutex;//�Ƿ�ʹ���������
//ngx_use_accept_mutex��ʾ�Ƿ���Ҫͨ����accept�����������Ⱥ���⡣��nginx worker������>1ʱ�������ļ��д�accept_mutexʱ�������־��Ϊ1  
ngx_uint_t            ngx_accept_events;
ngx_uint_t            ngx_accept_mutex_held;//���Ѿ��õ���accept����
ngx_msec_t            ngx_accept_mutex_delay;

ngx_int_t             ngx_accept_disabled;
//ngx_accept_disabled��ʾ��ʱ�����ɣ�û��Ҫ�ٴ����������ˣ�������nginx.conf����������ÿһ��
//ginx worker�����ܹ��������������������ﵽ�������7/8ʱ��ngx_accept_disabledΪ����˵����nginx worker���̷ǳ���æ��
//������ȥ���������ӣ���Ҳ�Ǹ��򵥵ĸ��ؾ���  

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



static ngx_command_t  ngx_events_commands[] = {//��ngx_events_moduleģ�����������ָ�����¡�
    { ngx_string("events"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_events_block,//ָ���set����,�����ngx_conf_parse�����������������
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_events_module_ctx = {
    ngx_string("events"),//events {}�����Ŀ������û��ɶ������
    NULL,//create_conf
    NULL//init_conf�ص�
};

ngx_module_t  ngx_events_module = {//ngx_modules�������õģ�����������¼��Ĺ���ģ�飬������ʼ�������¼�ģ�����ngx_event_core_module��
    NGX_MODULE_V1,
    &ngx_events_module_ctx,                /* module context */
    ngx_events_commands,                   /* module directives *///��ģ���ָ���б�
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master *///Ϊ��
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
      NGX_EVENT_CONF|NGX_CONF_TAKE1,//ֻ��һ������
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
//ngx_events_module��ngx_event_core_module����
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
{//����һ���¼��Ͷ�ʱ����
    ngx_uint_t  flags;
    ngx_msec_t  timer, delta;
//Ҳ����˵�������ļ���ʹ����timer_resolutionָ���epoll_wait��ʹ���ź��жϵĻ�����������ʱ����
//����ʹ�ö�ʱ�����������Сʱ����Ϊepoll_wait��ʱʱ����������ʱ������ʱ����ngx_event_process_init����
    if (ngx_timer_resolution) {//�����������������ʱ�侫ȷ��.
        timer = NGX_TIMER_INFINITE;//��Ϊ����������Ϊngx_timer_resolution�����������˼�ǣ������ж�ʱ����Ҫ��ʱ�ˣ�������Ҫ�ȵ���ʱ���������С�
//Ϊ�β�ȡ����Сֵ����Ϊû��ȡ��Сֵ����������
        flags = 0;
    } else {//��Ϊû�ж�ʱ�������Ե��ú������Сʱ��
        timer = ngx_event_find_timer();//������С�Ļ�Ҫ��ó�ʱ�������epoll_wait�����ô���ˣ���Ȼ����
        flags = NGX_UPDATE_TIME;//����epoll_wait�ȴ�֮����Ҫ����һ��ʱ�䡣
#if (NGX_THREADS)
        if (timer == NGX_TIMER_INFINITE || timer > 500) {
            timer = 500;
        }
#endif
    }
//���������Ҫ��accept�������Ⱥ���⣬���Ȼ�������ڻ�������ڲ����Ὣ�����������epoll�ġ�
//�������һ��
    if (ngx_use_accept_mutex) {//listupdate������û�п����ģ�Ҳ���޷����⾪Ⱥ����
        if (ngx_accept_disabled > 0) {//����Ƶ�ʵģ�����7/8�Ϳ���
            ngx_accept_disabled--;
        } else {//��ȡ��,���ҽ�����SOCK����epoll��һ����ν��м�أ����򲻼��������
            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR) {
                return;
            }

            if (ngx_accept_mutex_held) {//�õ����ˣ�����͵����ŵ�
                flags |= NGX_POST_EVENTS;
//�õ����Ļ�����flagΪNGX_POST_EVENTS������ζ��ngx_process_events�����У�
//�κ��¼������Ӻ������accept�¼����ŵ�ngx_posted_accept_events�����У�
//epollin|epollout�¼����ŵ�ngx_posted_events������  ����ʵ�����뾡���ͷ���������Ա����Ľ�����
            } else {//���û���õ���
                if (timer == NGX_TIMER_INFINITE || timer > ngx_accept_mutex_delay) {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }//���û������accept_mutex on �� ��ô�ͻ��о�Ⱥ�������

    delta = ngx_current_msec;//��ǰʱ��
//����epoll_wait�������Ҫaccept�����õ��ˣ���ͬʱ���listening fd�������ؿɶ���д�¼���������Ҫ����ngx_posted_accept_events����
    (void) ngx_process_events(cycle, timer, flags);//����ngx_epoll_process_events

    delta = ngx_current_msec - delta;//�����¼�ʱ���
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "timer delta: %M", delta);

    if (ngx_posted_accept_events) {//���accept�Ӻ��������������ݣ���ô���ȸϽ�accept֮��Ȼ�������ͷ������ñ�Ľ����ܷ���
        ngx_event_process_posted(cycle, &ngx_posted_accept_events);//ngx_event_accept
    }
    if (ngx_accept_mutex_held) {//���õ�������������µ����ӣ����Ѿ�accept�ˣ�����
        ngx_shmtx_unlock(&ngx_accept_mutex);
    }

    if (delta) {//��!����ղŵ�ngx_process_eventsû�л���̫�ã�1�붼û�У���Ѿ�Ķ�����ȥ����ʱ������Ϊѹ��û�г�ʱ�Ŀ϶���ţ��
        ngx_event_expire_timers();//�ѳ�ʱ�Ļص���
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "posted events %p", ngx_posted_events);
    if (ngx_posted_events) {//��Ҳ�ͷ��ˣ�������Ҫ����һ�����ݶ�д�¼���
        if (ngx_threaded) {
            ngx_wakeup_worker_thread(cycle);
        } else {
            ngx_event_process_posted(cycle, &ngx_posted_events);//����ҵ����еĶ�д�¼���
        }
    }
}


ngx_int_t
ngx_handle_read_event(ngx_event_t *rev, ngx_uint_t flags)
{//��һ�����Ӽ���ɶ��¼������С�
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {
        /* kqueue, epoll */
        if (!rev->active && !rev->ready) {//�������Ծ����û�����ý�ȥ����ready��û�����ݿ��Զ����ͼ��뵽epoll
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
{//ֻ��ע����һ�¶�д�¼���
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
            if (ngx_add_event(wev, NGX_WRITE_EVENT,
                              NGX_CLEAR_EVENT | (lowat ? NGX_LOWAT_EVENT : 0))
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;

    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {

        /* select, poll, /dev/poll */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->active && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
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
{//ngx_init_cycle��������������������ģ���ģ���ʼ��������������������������õġ�
//��������Ĺ����ڴ棬����ngx_accept_mutex_ptr�ȡ�
    void              ***cf;
    u_char              *shared;
    size_t               size, cl;
    ngx_shm_t            shm;
    ngx_time_t          *tp;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;

    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);//�õ���create_conf����������������
    if (cf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no \"events\" section in configuration");
        return NGX_ERROR;
    }

    ecf = (*cf)[ngx_event_core_module.ctx_index];//�õ���ģ������ã�����create_conf Ҳ����ngx_event_create_conf���ص���������
    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "using the \"%s\" event method", ecf->name);
    }
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//�õ���ʦү������

    ngx_timer_resolution = ccf->timer_resolution;//�Ƿ�Ҫ������ȷʱ��ѡ�ֵ�Ǿ�ȷ��

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
	//Ϊ�������ݽṹ���乲���ڴ档
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
{//��ʱ���źŴ��������ٴ����ú�epoll_wait�ȴ������Ϊ1����ʾ��ʱ�����ˣ���Ҫ����ʱ��
    ngx_event_timer_alarm = 1;

#if 1
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ngx_cycle->log, 0, "timer signal");
#endif
}

#endif


static ngx_int_t
ngx_event_process_init(ngx_cycle_t *cycle)
{//���̳�ʼ���󣬻����ÿ��ģ��Ľ��̳�ʼ��������
	//���ü���SOCK��handler�ص������������������accept��Ȼ������д�¼���

    ngx_uint_t           m, i;
    ngx_event_t         *rev, *wev;
    ngx_listening_t     *ls;
    ngx_connection_t    *c, *next, *old;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;
    ngx_event_module_t  *module;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//�õ���ʦү������
    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);//�ȵõ�ngx_events_module���������ã�Ȼ��õ�core�¼��ṹ������

    if (ccf->master && ccf->worker_processes > 1 && ecf->accept_mutex) {//�������������1��������accept_mutex��0
        ngx_use_accept_mutex = 1;//Ҫʹ��accept��
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
//��ʼ��������ṹ
    if (ngx_event_timer_init(cycle->log) == NGX_ERROR) {
        return NGX_ERROR;
    }

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }
        if (ngx_modules[m]->ctx_index != ecf->use) {//�ҵ���ʹ�õ��Ǹ��¼�ģ�͡�
            continue;//��use�������õ�
        }
        module = ngx_modules[m]->ctx;//���ʹ����ngx_epoll_module����ô��ctxΪngx_epoll_module_ctx����������ܶ�ص���
        if (module->actions.init(cycle, ngx_timer_resolution) != NGX_OK) {//����ngx_epoll_init
            /* fatal */
            exit(2);
        }

        break;
    }

#if !(NGX_WIN32)
//������ngx_timer_resolution�Ż����ö�ʱ����ָ��ʱ��鷢������epoll�������ó�ʱʱ���ˣ���Ϊ��ʱ���ᴥ�������ص�
    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT)) {
        struct sigaction  sa;
        struct itimerval  itv;

        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = ngx_timer_signal_handler;
        sigemptyset(&sa.sa_mask);
//ע�ᶨʱ���ص�����Ϊngx_timer_signal_handler
        if (sigaction(SIGALRM, &sa, NULL) == -1) {//ע���õĲ���signal����������ÿ�ζ����ã����ᱻ�ں�ÿ������
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "sigaction(SIGALRM) failed");
            return NGX_ERROR;
        }

        itv.it_interval.tv_sec = ngx_timer_resolution / 1000;
        itv.it_interval.tv_usec = (ngx_timer_resolution % 1000) * 1000;
        itv.it_value.tv_sec = ngx_timer_resolution / 1000;
        itv.it_value.tv_usec = (ngx_timer_resolution % 1000 ) * 1000;
//���ö�ʱ��
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
//cycle->connection_nΪconnections������Ĵ�С
    cycle->connections = ngx_alloc(sizeof(ngx_connection_t) * cycle->connection_n, cycle->log);
    if (cycle->connections == NULL) {
        return NGX_ERROR;
    }

    c = cycle->connections;
//������¼��ڴ�
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
//����д�¼��ڴ�
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
        c[i].data = next;//���������γ�����,��ǰ����ָ
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
        c->listening = &ls[i];//ָ���������������������listing�ṹ
        ls[i].connection = c;//ָ������ָ�����ӡ�ע��һ������SOCK�����кܶ�����ָ���Լ���������connectionָֻ������ָ���Ǹ����ӣ�������epoll��

        rev = c->read;//ʵ���ϣ�c[i].read = &cycle->read_events[i];���ӽṹ�����read�¼�ָ��cycle->read_events��Ӧ��
//Ҳ����˵��cycle->read_events���¼��أ�һ�����ӡ������Ӷ�Ӧ�ġ�
        rev->log = c->log;
        rev->accept = 1;//�����Ȼ�Ǽ���fd��

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
//���ü���SOCK���¼��ص�������˺�������accept
        rev->handler = ngx_event_accept;//���ü���SOCK��handler�ص������������������accept��Ȼ������д�¼���

        if (ngx_use_accept_mutex) {
            continue;//�����ngx_use_accept_mutex����ô�����Ȳ��üӵ�epoll����Ϊÿ��ѭ������������ӵģ����߻�ȥ���ġ�
        }

        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_add_conn(c) == NGX_ERROR) {
                return NGX_ERROR;
            }

        } else {//Ĭ���Ƚ�������Ӽӽ�ȥ��˵���������жϡ���Ϊû��ngx_use_accept_mutex
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
{//��������events {}���ʱ�򣬵��ñ�ָ���set������
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_event_module_t   *m;

    /* count the number of the event modules and set up their indices *///��index�ĸ�����
    ngx_event_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {//����ngx_event_core_module���ģ��
            continue;
        }//��NGX_EVENT_MODULE���͵�ģ����б�ţ�������ţ�������ΪEVENT_MODULE���͵�ģ��ı�ţ�����ȫ�ֵ�
        ngx_modules[i]->ctx_index = ngx_event_max_module++;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));//����һ��������ָ��
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *(void **) conf = ctx;//���ϲ����������Ľṹ��

    for (i = 0; ngx_modules[i]; i++) {//����ÿһ��NGX_EVENT_MODULE���͵�ģ�飬������create_conf�ص�
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }
        m = ngx_modules[i]->ctx;//
        if (m->create_conf) {//ngx_modules[i]->ctx_index�Ǹ�ģ���ڸ����͵���š�
            (*ctx)[ngx_modules[i]->ctx_index] = m->create_conf(cf->cycle);//����ֵ��λ�������
            if ((*ctx)[ngx_modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }
    pcf = *cf;//����һ�������ģ����еݹ�����������������ṹ������ݡ�
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;
    rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;//��ԭ������
    if (rv != NGX_CONF_OK)
        return rv;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }
        m = ngx_modules[i]->ctx;
        if (m->init_conf) {//����ģ��ĳ�ʼ������ʱ�Ѿ�������������
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
{//���������
    ngx_event_conf_t  *ecf = conf;

    ngx_str_t  *value;

    if (ecf->connections != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }
    if (ngx_strcmp(cmd->name.data, "connections") == 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"connections\" directive is deprecated, use the \"worker_connections\" directive instead");
    }

    value = cf->args->elts;//��������
    ecf->connections = ngx_atoi(value[1].data, value[1].len);//��һ������Ӧ�������������õ�eventģ��ı����С�
    if (ecf->connections == (ngx_uint_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid number \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    cf->cycle->connection_n = ecf->connections;//ͬʱ���ø�cycle��connection_n
    return NGX_CONF_OK;
}


static char *
ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//ɨһ�����õ�ģ�飬���������ҵ�ģ�顣Ȼ��������ȥ��
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
        module = ngx_modules[m]->ctx;//�ҵ���Ҫ���õ�ģ�顣
        if (module->name->len == value[1].len) {
            if (ngx_strcmp(module->name->data, value[1].data) == 0) {
                ecf->use = ngx_modules[m]->ctx_index;//����Ϊ��ģ�����������͵��±�
                ecf->name = module->name->data;//����

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
{//����events{}��ʱ�򣬻�Ԥ�ȵ������
    ngx_event_conf_t  *ecf;
    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL) {
        return NULL;
    }
	//����������õ��ڴ棬���г�ʼ��
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
    return ecf;//��������ڴ���ϲ㡣�ϲ�����õ���ctx�����
}


static char *
ngx_event_init_conf(ngx_cycle_t *cycle, void *conf)
{//��ʼ��ģ�飬��ʱ�Ѿ���������ص������ˡ�����ֻ��������һЩ��ʼֵ
    ngx_event_conf_t  *ecf = conf;//�õ�����ngx_event_create_conf�������õ����ýṹ
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

//�����ж�Ӧ��ʹ����һ������ģ�顣������module�����ϡ�
    module = NULL;
#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    fd = epoll_create(100);
    if (fd != -1) {//��Ȼ��epoll����֮
        close(fd);
        module = &ngx_epoll_module;//������Ҫ��epoll������epoll
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
//�õ�����Ӧ��ʹ�õ�ģ��
    if (module == NULL) {//���û�еõ������ҵ�һ�����ֲ�����event_core_name��ģ��
        for (i = 0; ngx_modules[i]; i++) {
            if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
                continue;
            }
            event_module = ngx_modules[i]->ctx;
            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0) {
                continue;//�������������event_core���ͼ�������ô�����ھ�OK�ˣ�����˵��һ�����п
            }
            module = ngx_modules[i];
            break;
        }
    }
    if (module == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }
	
    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);//���û������ֵ��������Ϊ�����Ĭ��ֵ
    cycle->connection_n = ecf->connections;
    ngx_conf_init_uint_value(ecf->use, module->ctx_index);
    event_module = module->ctx;//�õ���ģ��������ģ�����ngx_event_module_t ngx_event_core_module_ctx 
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
