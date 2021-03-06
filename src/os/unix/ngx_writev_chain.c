
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
{//调用writev一次发送多个缓冲区，如果没有发送完毕，则返回剩下的链接结构头部。
//ngx_chain_writer调用这里，调用方式为 ctx->out = c->send_chain(c, ctx->out, ctx->limit);
//第二个参数为要发送的数据
    u_char        *prev;
    ssize_t        n, size, sent;
    off_t          send, prev_send;
    ngx_uint_t     eintr, complete;
    ngx_err_t      err;
    ngx_array_t    vec;
    ngx_chain_t   *cl;
    ngx_event_t   *wev;
    struct iovec  *iov, iovs[NGX_IOVS];
    wev = c->write;//拿到这个连接的写事件结构
    if (!wev->ready) {//连接还没准备好，返回当前的节点。
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
        limit = NGX_MAX_SIZE_T_VALUE - ngx_pagesize;//够大了，最大的整数
    }
    send = 0;
    complete = 0;
    vec.elts = iovs;//数组
    vec.size = sizeof(struct iovec);
    vec.nalloc = NGX_IOVS;//申请了这么多。
    vec.pool = c->pool;

    for ( ;; ) {
        prev = NULL;
        iov = NULL;
        eintr = 0;
        prev_send = send;//之前已经发送了这么多
        vec.nelts = 0;
        /* create the iovec and coalesce the neighbouring bufs */
		//循环发送数据，一次一块IOV_MAX数目的缓冲区。
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
            size = cl->buf->last - cl->buf->pos;//计算这个节点的大小
            if (send + size > limit) {//超过最大发送大小。截断，这次只发送这么多
                size = (ssize_t) (limit - send);
            }
            if (prev == cl->buf->pos) {//如果还是等于刚才的位置，那就复用
                iov->iov_len += size;
            } else {//否则要新增一个节点。返回之
                iov = ngx_array_push(&vec);
                if (iov == NULL) {
                    return NGX_CHAIN_ERROR;
                }
                iov->iov_base = (void *) cl->buf->pos;//从这里开始
                iov->iov_len = size;//有这么多我要发送
            }
            prev = cl->buf->pos + size;//记录刚才发到了这个位置，为指针哈。
            send += size;//增加已经记录的数据长度。
        }

        n = writev(c->fd, vec.elts, vec.nelts);//调用writev发送这些数据，返回发送的数据大小
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
        sent = n > 0 ? n : 0;//记录发送的数据大小。
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0, "writev: %z", sent);
        if (send - prev_send == sent) {//啥意思�?之前没有发送任何数据吗
            complete = 1;
        }
        c->sent += sent;//递增统计数据，这个链接上发送的数据大小

        for (cl = in; cl; cl = cl->next) {
			//又遍历一次这个链接，为了找到那块只成功发送了一部分数据的内存块，从它继续开始发送。
            if (ngx_buf_special(cl->buf)) {
                continue;
            }
            if (sent == 0) {
                break;
            }
            size = cl->buf->last - cl->buf->pos;
            if (sent >= size) {
                sent -= size;//标记后面还有多少数据是我发送过的
                cl->buf->pos = cl->buf->last;//清空这段内存。继续找下一个
                continue;
            }
            cl->buf->pos += sent;//这块内存没有完全发送完毕，悲剧，下回得从这里开始。

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
        in = cl;//继续刚才没有发送完毕的内存。继续发送
    }
}
