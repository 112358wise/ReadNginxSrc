
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


static ngx_int_t ngx_enable_accept_events(ngx_cycle_t *cycle);
static ngx_int_t ngx_disable_accept_events(ngx_cycle_t *cycle);
static void ngx_close_accepted_connection(ngx_connection_t *c);

//���������Ӻ󣬻���ö�ngx_event_t�ṹread��handler�ص�������socket������Ϊ���������
//�������̳�ʼ����ʱ������ngx_event_process_initģ���ʼ����������Ϊngx_event_accept������accept����
//�������ӵ�ʱ�������������accept.
//����Ὣ�����ӷ���epoll�������ɶ���д�¼���Ȼ�����ngx_http_init_connection
void ngx_event_accept(ngx_event_t *ev)
{
    socklen_t          socklen;
    ngx_err_t          err;
    ngx_log_t         *log;
    ngx_socket_t       s;
    ngx_event_t       *rev, *wev;
    ngx_listening_t   *ls;
    ngx_connection_t  *c, *lc;
    ngx_event_conf_t  *ecf;
    u_char             sa[NGX_SOCKADDRLEN];

    ecf = ngx_event_get_conf(ngx_cycle->conf_ctx, ngx_event_core_module);//�ȵõ�ngx_events_module��Ȼ���ٵõ������coreģ��
    if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
        ev->available = 1;
    } else if (!(ngx_event_flags & NGX_USE_KQUEUE_EVENT)) {
        ev->available = ecf->multi_accept;//һ�ξ������꣬Ĭ��Ϊ0��
    }
    lc = ev->data;//�õ�����¼�����������
    ls = lc->listening;//�Ӷ��õ����������ָ��listening �ṹ
    ev->ready = 0;
    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,"accept on %V, ready: %d", &ls->addr_text, ev->available);
    do {//��������пɶ��¼��ˣ��ǿ��ܿ��Զ��ܶ��ˣ����Ե���ѭ��
        socklen = NGX_SOCKADDRLEN;
        s = accept(lc->fd, (struct sockaddr *) sa, &socklen);//��һ��������
        if (s == -1) {//ʧ��
            err = ngx_socket_errno;
            if (err == NGX_EAGAIN) {//û�������
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, err, "accept() not ready");
                return;
            }
            ngx_log_error((ngx_uint_t) ((err == NGX_ECONNABORTED) ? NGX_LOG_ERR : NGX_LOG_ALERT), ev->log, err, "accept() failed");
            if (err == NGX_ECONNABORTED) {
                if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
                    ev->available--;//kqueue�Ļ����ܽӶ��
                }
                if (ev->available) {
                    continue;
                }
            }
            return;
        }
//accept�ɹ�
#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_accepted, 1);
#endif
        ngx_accept_disabled = ngx_cycle->connection_n / 8 - ngx_cycle->free_connection_n;
//����ʹ�õ�������ռ����nginx.conf�����õ�worker_connections������7/8����ʱ��ngx_accept_disabledΪ����0��
//�˺�����ѭ������Ͳ����ٽ���accept�����ǵݼ�1�������൱������������̶���һ��accept�Ļ���ɡ�
//�������ֻ��accept_mutex on ���ô�ʱ����Ч������Ļ���Ĭ�ϻ᲻�ϼ�����

        c = ngx_get_connection(s, ev->log);//�õ�һ�����е�����
        if (c == NULL) {
            if (ngx_close_socket(s) == -1) {
                ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_socket_errno,
                              ngx_close_socket_n " failed");
            }
            return;
        }

#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_active, 1);
#endif

        c->pool = ngx_create_pool(ls->pool_size, ev->log);
//Ϊ��������½�һ��pool�������Ǹ����ӹرպ�����ڴ��Ҳ�����ͷ��ˣ������������ڴ�й¶
        if (c->pool == NULL) {//�ڴ�����ʧ��
            ngx_close_accepted_connection(c);
            return;
        }

        c->sockaddr = ngx_palloc(c->pool, socklen);
        if (c->sockaddr == NULL) {
            ngx_close_accepted_connection(c);
            return;
        }

        ngx_memcpy(c->sockaddr, sa, socklen);
        log = ngx_palloc(c->pool, sizeof(ngx_log_t));
        if (log == NULL) {
            ngx_close_accepted_connection(c);
            return;
        }
        /* set a blocking mode for aio and non-blocking mode for others */
        if (ngx_inherited_nonblocking) {
            if (ngx_event_flags & NGX_USE_AIO_EVENT) {
                if (ngx_blocking(s) == -1) {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_socket_errno,
                                  ngx_blocking_n " failed");
                    ngx_close_accepted_connection(c);
                    return;
                }
            }
        } else {//����Ϊ��������
            if (!(ngx_event_flags & (NGX_USE_AIO_EVENT|NGX_USE_RTSIG_EVENT))) {
                if (ngx_nonblocking(s) == -1) {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_socket_errno,
                                  ngx_nonblocking_n " failed");
                    ngx_close_accepted_connection(c);
                    return;
                }
            }
        }
        *log = ls->log;
        c->recv = ngx_recv;//k ngx_unix_recv  ����ʵ����ngx_ssl_recv
        c->send = ngx_send;//k ngx_unix_send , ��ʵ����ngx_ssl_write
        c->recv_chain = ngx_recv_chain;//k ngx_readv_chain
        c->send_chain = ngx_send_chain;//k ngx_writev_chain
/*ngx_io = ngx_os_io ;//�൱�����IO�Ǹ�os��صġ�
ngx_os_io_t ngx_os_io = {
    ngx_unix_recv,
    ngx_readv_chain,
    ngx_udp_unix_recv,
    ngx_unix_send,
    ngx_writev_chain,
    0
};*/
        c->log = log;
        c->pool->log = log;
        c->socklen = socklen;
        c->listening = ls;//����������ӣ���ָһ���������������listening�ṹ��ָ�����Ǵ��ĸ�listenSOCK accept������
        c->local_sockaddr = ls->sockaddr;
        c->unexpected_eof = 1;
#if (NGX_HAVE_UNIX_DOMAIN)
        if (c->sockaddr->sa_family == AF_UNIX) {
            c->tcp_nopush = NGX_TCP_NOPUSH_DISABLED;
            c->tcp_nodelay = NGX_TCP_NODELAY_DISABLED;
        }
#endif
        rev = c->read;//��������ӵĶ�д�¼�
        wev = c->write;
        wev->ready = 1;// д�¼�����ʾ�Ѿ�accept�� ?
        if (ngx_event_flags & (NGX_USE_AIO_EVENT|NGX_USE_RTSIG_EVENT)) {
            /* rtsig, aio, iocp */
            rev->ready = 1;
        }
        if (ev->deferred_accept) {
//�������deferredģʽ���ں����������ֽ������Ӻ󣬲�������֪ͨ����������ӿɶ������ǵȴ�����һ���ɶ����ݰ���֪ͨ,��ˣ���ʱ���пɶ��¼���
            rev->ready = 1;//��ؿ��Զ���
        }
        rev->log = log;
        wev->log = log;
        /*
         * TODO: MT: - ngx_atomic_fetch_add()
         *             or protection by critical section or light mutex
         *
         * TODO: MP: - allocated in a shared memory
         *           - ngx_atomic_fetch_add()
         *             or protection by critical section or light mutex
         */
        c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);
#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_handled, 1);
#endif
#if (NGX_THREADS)
        rev->lock = &c->lock;//��д�¼������������ϵ��������ڶ��߳�
        wev->lock = &c->lock;
        rev->own_lock = &c->lock;
        wev->own_lock = &c->lock;
#endif
        if (ls->addr_ntop) {
            c->addr_text.data = ngx_pnalloc(c->pool, ls->addr_text_max_len);
            if (c->addr_text.data == NULL) {
                ngx_close_accepted_connection(c);
                return;
            }
            c->addr_text.len = ngx_sock_ntop(c->sockaddr, c->addr_text.data, ls->addr_text_max_len, 0);
            if (c->addr_text.len == 0) {
                ngx_close_accepted_connection(c);
                return;
            }
        }
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, log, 0, "*%d accept: %V fd:%d", c->number, &c->addr_text, s);
        if (ngx_add_conn && (ngx_event_flags & NGX_USE_EPOLL_EVENT) == 0) {
            if (ngx_add_conn(c) == NGX_ERROR) {//���ڼ����ˣ�����û���ûص��أ�����û�£����������̣��������µġ�����ͼ�
//���ʹ��epoll����ϲ��.ngx_epoll_add_connection ���ñ�Ե������ע��EPOLLIN|EPOLLOUT|EPOLLET
                ngx_close_accepted_connection(c);
                return;
            }
        }
        log->data = NULL;
        log->handler = NULL;
//ע�⣬������ӵĶ�д�¼��ص������ʱ��û�����ã�Ϊʲô��? ��Ϊ�˴���ͨ�õģ�
//��ֻ����������ӣ�����epoll����������������������ˣ���http����ftp����httpsɶ�ġ�����ľ͵ÿ����listen sock������ʲô�ˣ�����http,ftpɶ�ġ�
//����˵: ����һ�����Ӻ�Ӧ����ô���أ�Ӧ�ý��ж�Ӧ�ĳ�ʼ��������ô��ʼ��? ����ʱ����ʲô������ô��ʼ����
        ls->handler(c);//ָ��ngx_http_init_connection���ͷ����ngx_http_commands -> ngx_http_block���õ�
// ngx_http_block ��������� ngx_http_optimize_servers �����������listening��connection��صı��������˳�ʼ���͵��ţ�
//�������� ngx_http_add_listening ����ngx_http_init_listening���ã� ��ע����listening �� handler Ϊ ngx_http_init_connection
        if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
            ev->available--;
        }

    } while (ev->available);//һ�ο��ԽӶ����ֱ��û�пɶ�����
}

/*
���accept�������worker����һ�����Եõ��������
����������������̣��������̷��أ���ȡ�ɹ��Ļ�ngx_accept_mutex_held����Ϊ1��
�õ���,��ô��������ᱻ�ŵ������̵�epoll���ˣ��������������ᱻ��epoll��ȡ����  

*/
ngx_int_t ngx_trylock_accept_mutex(ngx_cycle_t *cycle)
{
    if (ngx_shmtx_trylock(&ngx_accept_mutex)) {//�ļ�������spinlock
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "accept mutex locked");
        if (ngx_accept_mutex_held && ngx_accept_events == 0 && !(ngx_event_flags & NGX_USE_RTSIG_EVENT)) {//ע������и�����
            return NGX_OK;
        }
//��������SOCK �Ķ��¼����뵽epoll����Ϊ���ǻ���������������ǿ��Խ���accept�ˣ����ǽ���accept�¼�����epoll
        if (ngx_enable_accept_events(cycle) == NGX_ERROR) {
            ngx_shmtx_unlock(&ngx_accept_mutex);
            return NGX_ERROR;
        }
        ngx_accept_events = 0;
        ngx_accept_mutex_held = 1;//���õ��ˣ�������Է�����
        return NGX_OK;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "accept mutex lock failed: %ui", ngx_accept_mutex_held);
    if (ngx_accept_mutex_held) {
//ngx_accept_mutex_held�������ǲ��ᱻ�ı�ģ���������ʾ������ղ��һ�ù�һ�����ˣ������û���õ��������ҵ�ɾ��epollע����С�
//������Ҫ����һ�㣬��ʼ��ʱ����û�м���epoll�ģ������һ��û���õ�������ô����Ͳ���Ҫɾ��������������ζ�û���õ�������Ҳ����Ҫ�ظ�ɾ��
        if (ngx_disable_accept_events(cycle) == NGX_ERROR) {
            return NGX_ERROR;
        }

        ngx_accept_mutex_held = 0;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_enable_accept_events(ngx_cycle_t *cycle)
{//��cycle->listening��ÿ���ɶ��¼������뵽epoll
    ngx_uint_t         i;
    ngx_listening_t   *ls;
    ngx_connection_t  *c;

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ls[i].connection;
        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_add_conn(c) == NGX_ERROR) {
                return NGX_ERROR;
            }
        } else {//��������¼������ȥ
            if (ngx_add_event(c->read, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_disable_accept_events(ngx_cycle_t *cycle)
{//ɾ������SOCK�Ķ��¼���һ����û�л������ʱ�򣬵���ɾ������¼����У���ȻԽλ��
    ngx_uint_t         i;
    ngx_listening_t   *ls;
    ngx_connection_t  *c;

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ls[i].connection;
        if (!c->read->active) {
            continue;
        }
        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_del_conn(c, NGX_DISABLE_EVENT) == NGX_ERROR) {
                return NGX_ERROR;
            }

        } else {
            if (ngx_del_event(c->read, NGX_READ_EVENT, NGX_DISABLE_EVENT)//ɾ�����¼����������һ��д�¼����������ܱ�Ť
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }
    }

    return NGX_OK;
}


static void
ngx_close_accepted_connection(ngx_connection_t *c)
{
    ngx_socket_t  fd;

    ngx_free_connection(c);

    fd = c->fd;
    c->fd = (ngx_socket_t) -1;

    if (ngx_close_socket(fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_socket_errno,
                      ngx_close_socket_n " failed");
    }

    if (c->pool) {
        ngx_destroy_pool(c->pool);
    }

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_active, -1);
#endif
}


u_char *
ngx_accept_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    return ngx_snprintf(buf, len, " while accepting new connection on %V",
                        log->data);
}
