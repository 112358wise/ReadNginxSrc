
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


typedef struct {
    ngx_uint_t  events;//����һ�η��ص��¼���These directives specify how many events may be passed to/from kernel, using appropriate method.
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
static void ngx_epoll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);
static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

static int                  ep = -1;//epoll���
static struct epoll_event  *event_list;//k epoll�ķ����¼���������
static ngx_uint_t           nevents;//����������С

#if (NGX_HAVE_FILE_AIO)

int                         ngx_eventfd = -1;
aio_context_t               ngx_aio_ctx = 0;

static ngx_event_t          ngx_eventfd_event;
static ngx_connection_t     ngx_eventfd_conn;

#endif

static ngx_str_t      epoll_name = ngx_string("epoll");

static ngx_command_t  ngx_epoll_commands[] = {
	//����һ�η��ص��¼���These directives specify how many events may be passed to/from kernel, using appropriate method.
    { ngx_string("epoll_events"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,//һ�����֡�
      0,
      offsetof(ngx_epoll_conf_t, events),
      NULL },

      ngx_null_command
};


ngx_event_module_t  ngx_epoll_module_ctx = {
    &epoll_name,
    ngx_epoll_create_conf,               /* create configuration */
    ngx_epoll_init_conf,                 /* init configuration */

    {
        ngx_epoll_add_event,             /* add an event */
        ngx_epoll_del_event,             /* delete an event */
        ngx_epoll_add_event,             /* enable an event */
        ngx_epoll_del_event,             /* disable an event */
        ngx_epoll_add_connection,        /* add an connection */
        ngx_epoll_del_connection,        /* delete an connection */
        NULL,                            /* process the changes */
        ngx_epoll_process_events,        /* process the events */
        ngx_epoll_init,                  /* init the events */
        ngx_epoll_done,                  /* done the events */
    }
};

ngx_module_t  ngx_epoll_module = {
    NGX_MODULE_V1,
    &ngx_epoll_module_ctx,               /* module context */
    ngx_epoll_commands,                  /* module directives */
    NGX_EVENT_MODULE,                    /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 *
 * Also we do not use eventfd() in glibc, because glibc supports it
 * since 2.8 version and glibc maps two syscalls eventfd() and eventfd2()
 * into single eventfd() function with different number of parameters.
 */

static long
io_setup(u_int nr_reqs, aio_context_t *ctx)
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}


static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}


static long
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
    struct timespec *tmo)
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}

#endif


static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{//����һ������: ���������¼�ģ�͵ĺ���ָ�� ngx_event_actions���Ժ���¼���صĵ��ö���ʹ��epoll�ġ�
    ngx_epoll_conf_t  *epcf;
    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);//�õ�����eventģ�鼯����������á�
    if (ep == -1) {//���epoll���Ϊ��Ч״̬���򴴽�֮
        ep = epoll_create(cycle->connection_n / 2);
        if (ep == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, "epoll_create() failed");
            return NGX_ERROR;
        }
#if (NGX_HAVE_FILE_AIO)//nginx��AIO��֧���ٴˡ���Ҫ2.6.22�汾���ϡ�
        {
        int                 n;
        struct epoll_event  ee;
        ngx_eventfd = syscall(SYS_eventfd, 0);
        if (ngx_eventfd == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "eventfd() failed");
            return NGX_ERROR;
        }
        n = 1;
        if (ioctl(ngx_eventfd, FIONBIO, &n) == -1) {//�����׽��ֵķ�����ģʽ
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "ioctl(eventfd, FIONBIO) failed");
        }
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,  "eventfd: %d", ngx_eventfd);
        n = io_setup(1024, &ngx_aio_ctx);
        if (n != 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, -n, "io_setup() failed");
            return NGX_ERROR;
        }
        ngx_eventfd_event.data = &ngx_eventfd_conn;
        ngx_eventfd_event.handler = ngx_epoll_eventfd_handler;//���¼��Ļص���
        ngx_eventfd_event.log = cycle->log;
        ngx_eventfd_event.active = 1;
        ngx_eventfd_conn.fd = ngx_eventfd;
        ngx_eventfd_conn.read = &ngx_eventfd_event;
        ngx_eventfd_conn.log = cycle->log;

        ee.events = EPOLLIN|EPOLLET;
        ee.data.ptr = &ngx_eventfd_conn;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) == -1) {//�����eventfd����epoll���档��AIO��epoll��������������ȡ״̬������������
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");
            return NGX_ERROR;
        }
        }
#endif
    }

    if (nevents < epcf->events) {
        if (event_list) {
            ngx_free(event_list);
        }
        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,  cycle->log);
        if (event_list == NULL) {
            return NGX_ERROR;
        }
    }

    nevents = epcf->events;
    ngx_io = ngx_os_io;
    ngx_event_actions = ngx_epoll_module_ctx.actions;

#if (NGX_HAVE_CLEAR_EVENT)
    ngx_event_flags = NGX_USE_CLEAR_EVENT
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT
#endif
                      |NGX_USE_GREEDY_EVENT
                      |NGX_USE_EPOLL_EVENT; //ʹ��epoll��ʽ����������¼���ѡȡ

    return NGX_OK;
}


static void
ngx_epoll_done(ngx_cycle_t *cycle)
{
    if (close(ep) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "epoll close() failed");
    }
    ep = -1;
#if (NGX_HAVE_FILE_AIO)
    if (io_destroy(ngx_aio_ctx) != 0) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,  "io_destroy() failed");
    }
    ngx_aio_ctx = 0;
#endif
    ngx_free(event_list);
    event_list = NULL;
    nevents = 0;
}


static ngx_int_t
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             events, prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    c = ev->data;

    events = (uint32_t) event;
//���Ҫע��POLLIN�¼���������c->write��ɶ��˼
    if (event == NGX_READ_EVENT) {//k POLLIN
        e = c->write;//��ֻ��Ϊ���ж�һ��������ӵ�����һ���¼��Ƿ���active������ǣ���˵���Ѿ���epoll���ˣ�����ֻ��EPOLL_CTL_MOD
        prev = EPOLLOUT;//������ô��ȷ����prev��EPOLLOUT�ء���
#if (NGX_READ_EVENT != EPOLLIN)
        events = EPOLLIN;
#endif

    } else {//����Ҫ���ӵ���д�¼�������������Ҫ�ж�һ�¶��¼���������¼���active�ģ���˵����ҪMOD�޸ģ�����������
        e = c->read;
        prev = EPOLLIN;
#if (NGX_WRITE_EVENT != EPOLLOUT)
        events = EPOLLOUT;
#endif
    }

    if (e->active) {//�Ѿ���ʹ���У��Ѿ���epoll�У���˵��֮ǰ�϶��ж�����д�¼��ģ���ô���ƶ�������ж�����ȷ��
        op = EPOLL_CTL_MOD;
        events |= prev;

    } else {
        op = EPOLL_CTL_ADD;//���򣬲���ʱ���ӣ������޸�
    }

    ee.events = events | (uint32_t) flags;
    ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0, "epoll add event: fd:%d op:%d ev:%08XD", c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno, "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }
    ev->active = 1;
#if 0
    ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif
    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;
    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicity the event
     * before the closing the file descriptor
     */
    if (flags & NGX_CLOSE_EVENT) {
        ev->active = 0;
        return NGX_OK;
    }

    c = ev->data;

    if (event == NGX_READ_EVENT) {//���Ҫɾ�����Ƕ��¼�
        e = c->write;//ΪʲôҪ�����෴���жϣ���Ϊ��ȷ��һ�����ӵĶ���д�¼��Ƿ��Ѿ�����epoll���Ӷ��������޸Ļ���ɾ��EPOLL_CTL_DEL
        prev = EPOLLOUT;//������д�¼�

    } else {//���Ҫɾ������д�¼��������¶��¼�
        e = c->read;
        prev = EPOLLIN;
    }

    if (e->active) {//����Ѿ���epoll�У���ΪMOD����
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    } else {//�����Ϊɾ������
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    ev->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event  ee;
    ee.events = EPOLLIN|EPOLLOUT|EPOLLET;//��д�¼�����Ҫ��ע�����ñ�Ե��������
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0, "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);
//��һ��epoll����𣬶����
    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno, "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }
    c->read->active = 1;//�����ʹ����
    c->write->active = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    int                 op;
    struct epoll_event  ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicity the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{//�������̲��ϵĵ������������û���¼�������������ʱ��ɶ�ġ�����У����ܷ�����л��߽��д���
//�������̵�ѭ�������������:ngx_process_events����ʵ����ָ����������Ƿ����¼����жϡ�
    int                events;
    uint32_t           revents;
    ngx_int_t          instance, i;
    ngx_uint_t         level;
    ngx_err_t          err;
    ngx_log_t         *log;
    ngx_event_t       *rev, *wev, **queue;
    ngx_connection_t  *c;

    /* NGX_TIMER_INFINITE == INFTIM */
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "epoll timer: %M", timer);
    events = epoll_wait(ep, event_list, (int) nevents, timer);
//����ʱ�ĵȴ�,���ʱ���ǵ�ǰ�����������С�Ļ�Ҫ��ó�ʱ��Ҳ����˵�������ʱ���ٴξͶ�ʱ�����˻����ж�����
    err = (events == -1) ? ngx_errno : 0;
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
//�������Ҫ�����ʱ�䣬����ngx_event_timer_alarm��ɶ��˼,ngx_timer_signal_handler,��ʱ�����ˣ���Ҫ����ʱ�䡣
        ngx_time_update();//��Ҫ����ʱ��
    }

    if (err) {//�д�
        if (err == NGX_EINTR) {//���ڶ�ʱ�����ˣ���ɶ��û�з��ء����ж���
            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;//���Ѿ������ˣ��»���֪ͨ�ң�����һ��
                return NGX_OK;
            }
            level = NGX_LOG_INFO;
        } else {
            level = NGX_LOG_ALERT;
        }//��ô�����ڴ�����Ϣ
        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

    if (events == 0) {//����һ�����鶼û��
        if (timer != NGX_TIMER_INFINITE) {//û���þ�ȷʱ�䣬timer���ڸոյȴ�֮ǰ�������������쳬ʱ���Ǹ���Ҫ��ã����ڵ���
            return NGX_OK;//����ǳ�ʱ�ˣ������Ǻ�����������С�ĳ�ʱ�ˡ���������ֵҲû���õ�
        }
		//����Ҳû�У��ֲ��ǳ�ʱ
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,  "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }
    ngx_mutex_lock(ngx_posted_events_mutex);//���̲߳�����
    log = cycle->log;

    for (i = 0; i < events; i++) {//�������������ÿ��Ԫ��
        c = event_list[i].data.ptr;//ȡ�ñ����ȥ������
        instance = (uintptr_t) c & 1;//����ɶ��˼��ȥ�����и�λ��������һ��λ,���õ�ʱ�������ô���õ�
        c = (ngx_connection_t *) ((uintptr_t) c & (uintptr_t) ~1);//ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);
//�����ý�epoll��ʱ������Ϊc|read->instance�ˣ�����ȥ�����λ����ԭ�����ӡ���Ϊ��֪�����ӵĵ�ַ���λ����Ϊ1����
        rev = c->read;//��ȡ��ɶ��¼��ṹ
		/*http://blog.csdn.net/dingyujie/article/details/7531498
		   fd�ڵ�ǰ����ʱ���-1����ζ����֮ǰ���¼�����ʱ���ѵ�ǰ����ر��ˣ�
		   ��close fd���ҵ�ǰ�¼���Ӧ�������ѱ��������ӳأ���ʱ�ô��¼��Ͳ�Ӧ�ô����ˣ����ϵ���
		   ��Σ����fd > 0,��ô�Ƿ񱾴��¼��Ϳ������������Ϳ�����Ϊ��һ���Ϸ����أ����Ƿ񶨵ġ�
		   �������Ǹ���һ���龰��
		   ��ǰ���¼������ǣ� A ... B ... C ...
		   ����A,B,C�Ǳ���epoll�ϱ�������һЩ�¼����������Ǵ�ʱȴ�໥ǣ����
		   A�¼�����ͻ���д���¼���B�¼��������ӵ�����C�¼���A�¼�����������upstream���ӣ���ʱ��Ҫ��Դ���ݣ�
		   Ȼ��A�¼�����ʱ����������ԭ��C��upstream�����ӹر���(����ͻ��˹رգ���ʱ��Ҫͬʱ�رյ�ȡԴ����)����Ȼ
		   C�¼��������Ӧ������Ҳ���������ӳ�(ע�⣬�ͻ���������upstream����ʹ��ͬһ���ӳ�)��
		   ��B�¼��е�����������ȡ���ӳ�ʱ���պ��õ���֮ǰC��upstream�����������ӽṹ����ǰ��Ҫ����C�¼���ʱ��
		   c->fd != -1����Ϊ�����ӱ�B�¼���ȥ���������ˣ���rev->instance��Bʹ��ʱ���Ѿ�����ֵȡ���ˣ����Դ�ʱC�¼�epoll��
		   Я����instance�Ͳ�����rev->instance�ˣ��������Ҳ��ʶ�����stale event�������������ˡ�
		  */
        if (c->fd == -1 || rev->instance != instance) {//����stale event ��
            /*
             * the stale event from a file descriptor that was just closed in this iteration
             */
            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,  "epoll: stale event %p", c);
            continue;
        }
        revents = event_list[i].events;//�õ��������¼�
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, log, 0, "epoll: fd:%d ev:%04XD d:%p",  c->fd, revents, event_list[i].data.ptr);
        if (revents & (EPOLLERR|EPOLLHUP)) {//������ִ��󣬴����־��debug��ʱ��
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, log, 0,  "epoll_wait() error on fd:%d ev:%04XD", c->fd, revents);
        }

         if ((revents & (EPOLLERR|EPOLLHUP)) && (revents & (EPOLLIN|EPOLLOUT)) == 0)
        {//����д��󣬶���û�ж�д�¼�����ô���Ӷ�д�¼�? 
            /*
             * if the error events were returned without EPOLLIN or EPOLLOUT,
             * then add these flags to handle the events at least in one
             * active handler
             */
            revents |= EPOLLIN|EPOLLOUT;
        }

        if ((revents & EPOLLIN) && rev->active) {
            if ((flags & NGX_POST_THREAD_EVENTS) && !rev->accept) {//���Ǽ���SOCK
                rev->posted_ready = 1;

            } else {
                rev->ready = 1;
            }

            if (flags & NGX_POST_EVENTS) {
                 queue = (ngx_event_t **) (rev->accept ? &ngx_posted_accept_events : &ngx_posted_events);
//���������NGX_POST_EVENTS�����ǰ������Ӻ�һ�����ӿɶ��¼����벻ͬ��������������ȴ����������¼��������ͷ���
                ngx_locked_post_event(rev, queue);//�����Ƿ��Ǽ���sock�����벻ͬ�Ķ�������

            } else {//����Ҫ�����¼��������Ǿ�ֱ�ӻص�����
                rev->handler(rev);//����������ӵĽṹ����������ҵ����fd.��Ȼ��Ϊʲô�������ﲻ���ж��Ƿ��Ǽ���sock�أ���Ϊ���ע���handle��ͬ
//������Ϊngx_event_accept����Ȼ�͵����˼����ĺ�����
            }
        }

        wev = c->write;
//�����и�bug������û���ж�instance�ˣ���1.0.9b�汾�Ѿ��޸��ˡ����������:http://forum.nginx.org/read.php?29,217919,217919#msg-217919
        if ((revents & EPOLLOUT) && wev->active) {//��д
            if (flags & NGX_POST_THREAD_EVENTS) {
                wev->posted_ready = 1;

            } else {
                wev->ready = 1;
            }

            if (flags & NGX_POST_EVENTS) {
                ngx_locked_post_event(wev, &ngx_posted_events);

            } else {
                wev->handler(wev);
	//����д�¼����Է�һ������۾�����ģ���Ϊ��ͬ�ĵط���handler��������ΪNGX_POST_EVENTS��accept���е��������Էֿ���
            }
        }
    }
    ngx_mutex_unlock(ngx_posted_events_mutex);//���̲߳�����
    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)

static void
ngx_epoll_eventfd_handler(ngx_event_t *ev)
{
    int               n;
    long              i, events;
    uint64_t          ready;
    ngx_err_t         err;
    ngx_event_t      *e;
    ngx_event_aio_t  *aio;
    struct io_event   event[64];
    struct timespec   ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");
    n = read(ngx_eventfd, &ready, 8);
    err = ngx_errno;
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);
    if (n != 8) {
        if (n == -1) {
            if (err == NGX_EAGAIN) {
                return;
            }
            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "read(eventfd) returned only %d bytes", n);
        return;
    }
    ts.tv_sec = 0;
    ts.tv_nsec = 0;

    while (ready) {//��ʱ0���ѯepoll��״̬��ʵ�����ǲ��ȴ���
        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "io_getevents: %l", events);
        if (events > 0) {
            ready -= events;
            for (i = 0; i < events; i++) {
                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %uXL %uXL %L %L",  event[i].data, event[i].obj,  event[i].res, event[i].res2);

                e = (ngx_event_t *) (uintptr_t) event[i].data;
                e->complete = 1;
                e->active = 0;
                e->ready = 1;
                aio = e->data;
                aio->res = event[i].res;
                ngx_post_event(e, &ngx_posted_events);//���¼������ˡ���������¼������С���д�Ѿ�����˵ġ�
            }
            continue;
        }
        if (events == 0) {
            return;
        }
        /* events < 0 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, -events, "io_getevents() failed");
        return;
    }
}

#endif


static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle)
{//ɶҲû��
    ngx_epoll_conf_t  *epcf;
    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL) {
        return NULL;
    }
    epcf->events = NGX_CONF_UNSET;
    return epcf;
}


static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_epoll_conf_t *epcf = conf;
    ngx_conf_init_uint_value(epcf->events, 512);
    return NGX_CONF_OK;
}
