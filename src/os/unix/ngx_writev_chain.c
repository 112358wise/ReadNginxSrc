
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (IOV_MAX > 64)
#define NGX_IOVS  64
#else
#define NGX_IOVS  IOV_MAX
#endif


ngx_chain_t *
ngx_writev_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{//����writevһ�η��Ͷ�������������û�з�����ϣ��򷵻�ʣ�µ����ӽṹͷ����
    u_char        *prev;
    ssize_t        n, size, sent;
    off_t          send, prev_send;
    ngx_uint_t     eintr, complete;
    ngx_err_t      err;
    ngx_array_t    vec;
    ngx_chain_t   *cl;
    ngx_event_t   *wev;
    struct iovec  *iov, iovs[NGX_IOVS];
    wev = c->write;//�õ�������ӵ�д�¼��ṹ
    if (!wev->ready) {//���ӻ�û׼���ã����ص�ǰ�Ľڵ㡣
        return in;
    }
#if (NGX_HAVE_KQUEUE)
    if ((ngx_event_flags & NGX_USE_KQUEUE_EVENT) && wev->pending_eof) {
        (void) ngx_connection_error(c, wev->kq_errno,  "kevent() reported about an closed connection");
        wev->error = 1;
        return NGX_CHAIN_ERROR;
    }
#endif
    /* the maximum limit size is the maximum size_t value - the page size */
    if (limit == 0 || limit > (off_t) (NGX_MAX_SIZE_T_VALUE - ngx_pagesize)) {
        limit = NGX_MAX_SIZE_T_VALUE - ngx_pagesize;//�����ˣ���������
    }
    send = 0;
    complete = 0;
    vec.elts = iovs;//����
    vec.size = sizeof(struct iovec);
    vec.nalloc = NGX_IOVS;//��������ô�ࡣ
    vec.pool = c->pool;

    for ( ;; ) {
        prev = NULL;
        iov = NULL;
        eintr = 0;
        prev_send = send;//֮ǰ�Ѿ���������ô��
        vec.nelts = 0;
        /* create the iovec and coalesce the neighbouring bufs */
		//ѭ���������ݣ�һ��һ��IOV_MAX��Ŀ�Ļ�������
        for (cl = in; cl && vec.nelts < IOV_MAX && send < limit; cl = cl->next)
        {
            if (ngx_buf_special(cl->buf)) {
                continue;
            }
#if 1
            if (!ngx_buf_in_memory(cl->buf)) {
                ngx_debug_point();
            }
#endif
            size = cl->buf->last - cl->buf->pos;//��������ڵ�Ĵ�С
            if (send + size > limit) {//��������ʹ�С���ض�
                size = (ssize_t) (limit - send);
            }
            if (prev == cl->buf->pos) {//������ǵ��ڸղŵ�λ�ã��Ǿ͸���
                iov->iov_len += size;

            } else {//����Ҫ����һ���ڵ㡣����֮
                iov = ngx_array_push(&vec);
                if (iov == NULL) {
                    return NGX_CHAIN_ERROR;
                }
                iov->iov_base = (void *) cl->buf->pos;//�����￪ʼ
                iov->iov_len = size;//����ô����Ҫ����
            }
            prev = cl->buf->pos + size;//��¼�ղŷ��������λ�ã�Ϊָ�����
            send += size;//�����Ѿ���¼�����ݳ��ȡ�
        }

        n = writev(c->fd, vec.elts, vec.nelts);//����writev������Щ���ݣ����ط��͵����ݴ�С
        if (n == -1) {
            err = ngx_errno;
            switch (err) {
            case NGX_EAGAIN:
                break;
            case NGX_EINTR:
                eintr = 1;
                break;
            default:
                wev->error = 1;
                (void) ngx_connection_error(c, err, "writev() failed");
                return NGX_CHAIN_ERROR;
            }
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,  "writev() not ready");
        }
        sent = n > 0 ? n : 0;//��¼���͵����ݴ�С��
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0, "writev: %z", sent);
        if (send - prev_send == sent) {//ɶ��˼�?֮ǰû�з����κ�������
            complete = 1;
        }
        c->sent += sent;//����ͳ�����ݣ���������Ϸ��͵����ݴ�С

        for (cl = in; cl; cl = cl->next) {//�ֱ���һ��������ӣ�Ϊ���ҵ��ǿ�ֻ�ɹ�������һ�������ݵ��ڴ�飬����������ʼ���͡�
            if (ngx_buf_special(cl->buf)) {
                continue;
            }
            if (sent == 0) {
                break;
            }
            size = cl->buf->last - cl->buf->pos;
            if (sent >= size) {
                sent -= size;//��Ǻ��滹�ж����������ҷ��͹���
                cl->buf->pos = cl->buf->last;//�������ڴ档��������һ��
                continue;
            }
            cl->buf->pos += sent;//����ڴ�û����ȫ������ϣ����磬�»صô����￪ʼ��

            break;
        }
        if (eintr) {
            continue;
        }
        if (!complete) {
            wev->ready = 0;
            return cl;
        }
        if (send >= limit || cl == NULL) {
            return cl;
        }
        in = cl;//�����ղ�û�з�����ϵ��ڴ档��������
    }
}
