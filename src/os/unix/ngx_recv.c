
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ssize_t
ngx_unix_recv(ngx_connection_t *c, u_char *buf, size_t size)
{//��Ҫ��c�϶����ݣ�����size��С�������buf��ͷ�ĵط������ı�epool����
//������Ϊ��һ�����ӿɶ���ʱ����á��������һ�ε���recvʱ��ȡ�����ݳ��ȡ�
    ssize_t       n;
    ngx_err_t     err;
    ngx_event_t  *rev;

    rev = c->read;
    do {
        n = recv(c->fd, buf, size, 0);
         ngx_log_debug3(NGX_LOG_DEBUG_EVENT, c->log, 0, "recv: fd:%d %d of %d", c->fd, n, size);
//These calls return the number of bytes received, or -1 if an error occurred. 
//The return value will be 0 when the peer has performed an orderly shutdown.
        if (n == 0) {//����0��ʾ�����ѶϿ���
            rev->ready = 0;//����Ϊ0��ʾû�����ݿ��Զ��ˣ��пɶ��¼���ʱ������ý�����,ngx_epoll_process_events������棬�пɶ��¼��ͱ��Ϊ1
            rev->eof = 1;//������ز�����
            return n;
        } else if (n > 0) {
            if ((size_t) n < size&& !(ngx_event_flags & NGX_USE_GREEDY_EVENT))
            {//���û������Ϊ̰�����ԣ�����һ��û�ж�ȡ��ϣ���������Ŷ��������Ļ�����Ϊ0��ʾû��������
                rev->ready = 0;
            }
            return n;
        }
//���С��2���϶�������
        err = ngx_socket_errno;

        if (err == NGX_EAGAIN || err == NGX_EINTR) {//�յ��ж�Ӱ��
             ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,"recv() not ready");
            n = NGX_AGAIN;
        } else {
            n = ngx_connection_error(c, err, "recv() failed");
            break;
        }

    } while (err == NGX_EINTR);

    rev->ready = 0;
    if (n == NGX_ERROR) {
        rev->error = 1;
    }

    return n;
}

