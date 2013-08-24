
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_do_read_client_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_write_request_body(ngx_http_request_t *r,
    ngx_chain_t *body);
static ngx_int_t ngx_http_read_discarded_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_test_expect(ngx_http_request_t *r);


/*
 * on completion ngx_http_read_client_request_body() adds to
 * r->request_body->bufs one or two bufs:
 *    *) one memory buf that was preread in r->header_in;����ڶ�ȡ����ͷ��ʱ���Ѿ�������һ�������ݣ����������
 *    *) one memory or file buf that contains the rest of the body û��Ԥ�������ݲ��ַ�������
 */
ngx_int_t ngx_http_read_client_request_body(ngx_http_request_t *r, ngx_http_client_body_handler_pt post_handler)
{//post_handler = ngx_http_upstream_init��NGINX��ȵ������BODYȫ����ȡ��Ϻ�Ž���upstream�ĳ�ʼ����GOOD
    size_t                     preread;
    ssize_t                    size;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, **next;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    r->main->count++;
    if (r->request_body || r->discard_body) {//discard_body�Ƿ���Ҫ�����������ݲ��֡������Ѿ����������ˡ���ֱ�ӻص�
        post_handler(r);//����Ҫ�����壬ֱ�ӵ���ngx_http_upstream_init
        return NGX_OK;
    }

    if (ngx_http_test_expect(r) != NGX_OK) {//����Ƿ���Ҫ����HTTP/1.1 100 Continue
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (rb == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->request_body = rb;//����������ṹ��������а�����䡣
    if (r->headers_in.content_length_n < 0) {
        post_handler(r);//�������Ҫ��ȡbody���֣�����С��0
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (r->headers_in.content_length_n == 0) {//���û������content_length_n
        if (r->request_body_in_file_only) {//client_body_in_file_only���ָ��ʼ�մ洢һ����������ʵ�嵽һ���ļ���ʹ��ֻ��0�ֽڡ�
            tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
            if (tf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            tf->file.fd = NGX_INVALID_FILE;
            tf->file.log = r->connection->log;
            tf->path = clcf->client_body_temp_path;
            tf->pool = r->pool;
            tf->warn = "a client request body is buffered to a temporary file";
            tf->log_level = r->request_body_file_log_level;
            tf->persistent = r->request_body_in_persistent_file;
            tf->clean = r->request_body_in_clean_file;
            if (r->request_body_file_group_access) {
                tf->access = 0660;
            }
            rb->temp_file = tf;//����һ����ʱ�ļ������洢POST������body����Ȼ���ֻ��0�ֽڣ�ɶ������û�С�
            if (ngx_create_temp_file(&tf->file, tf->path, tf->pool, tf->persistent, tf->clean, tf->access) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
        }
		//����ʵ�ʵ�content_length_n����Ϊ0��Ҳ�Ͳ���Ҫ���ж�ȡ�ˡ�ֱ�ӵ�init
        post_handler(r);//һ��GET����ֱ�ӵ�������
        return NGX_OK;
    }
	//�ðɣ����content_length_n����0 �ˣ�Ҳ���Ǹ�POST���������ȼ�¼һ�£�����POST���ݶ�ȡ��Ϻ���Ҫ���õ����ngx_http_upstream_init
    rb->post_handler = post_handler;
    /*
     * set by ngx_pcalloc():
     *     rb->bufs = NULL;
     *     rb->buf = NULL;
     *     rb->rest = 0;
     */
    preread = r->header_in->last - r->header_in->pos;//ʹ��֮ǰ�����ʣ�����ݣ����֮ǰԤ�������ݵĻ���
    if (preread) {//���֮ǰԤ���˶����������
        /* there is the pre-read part of the request body */
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http client request body preread %uz", preread);

        b = ngx_calloc_buf(r->pool);//����ngx_buf_t�ṹ�����ڴ洢Ԥ��������
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->temporary = 1;
        b->start = r->header_in->pos;//ֱ��ָ���Ѿ�Ԥ�������ݵĿ�ͷ�����POS�Ѿ�����������ú��˵ġ���ȡ����ͷ��HEADERS�����λ�ˡ�
        b->pos = r->header_in->pos;
        b->last = r->header_in->last;
        b->end = r->header_in->end;

        rb->bufs = ngx_alloc_chain_link(r->pool);//����һ��buf���ӱ������洢2��BODY����
        if (rb->bufs == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        rb->bufs->buf = b;//Ԥ����BODY���ַ�������
        rb->bufs->next = NULL;//���ಿ�ִ����ȡ��ʱ���������
        rb->buf = b;//ngx_http_request_body_t ��bufָ������µ�buf

        if ((off_t) preread >= r->headers_in.content_length_n) {//OK�����Ѿ������㹻��BODY�ˣ������뵽���������ֱ��ȥngx_http_upstream_init��
            /* the whole request body was pre-read */
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->request_length += r->headers_in.content_length_n;//ͳ��������ܳ���

            if (r->request_body_in_file_only) {//�����Ҫ��¼���ļ�����д���ļ�
                if (ngx_http_write_request_body(r, rb->bufs) != NGX_OK) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
            }
            post_handler(r);//���д���
            return NGX_OK;
        }
		//���Ԥ�������ݻ����������в�������û�ж��������
        /*
         * to not consider the body as pipelined request in
         * ngx_http_set_keepalive()
         */
        r->header_in->pos = r->header_in->last;//���ƣ����������д�룬Ҳ����׷�Ӱ�
        r->request_length += preread;//ͳ���ܳ���

        rb->rest = r->headers_in.content_length_n - preread;//���㻹�ж���������Ҫ��ȡ����ȥ�ղ�Ԥ���Ĳ��֡�
        if (rb->rest <= (off_t) (b->end - b->last)) {//�����Ҫ��ȡ�����ݴ�С�㹻���ɵ����ڵ�Ԥ��BUFFER���棬�Ǿ͸ɴ�������аɡ�
            /* the whole request body may be placed in r->header_in */
            rb->to_write = rb->bufs;//����д���һ��λ��rb->bufs->buf = b;
            r->read_event_handler = ngx_http_read_client_request_body_handler;//����Ϊ��ȡ�ͻ��˵�������
            return ngx_http_do_read_client_request_body(r);//���ϵ�ȥ��ʼ��ȡʣ��������
        }
		//���Ԥ����BUFFER�ݲ������еġ��Ǿ���Ҫ����һ���µ��ˡ�
        next = &rb->bufs->next;//����Ҫ��ȡ������Ϊ�ڶ���buf

    } else {
        b = NULL;//û��Ԥ������
        rb->rest = r->headers_in.content_length_n;//���������ȡ������Ϊ���еġ�
        next = &rb->bufs;//Ȼ������Ҫ��ȡ����������ŵ�λ��Ϊbufs�Ŀ�ͷ
    }

    size = clcf->client_body_buffer_size;//���õ���󻺳�����С��
    size += size >> 2;//���ô�СΪsize + 1/4*size,ʣ������ݲ�������������С��1.25����һ�ζ��꣨1.25�����Ǿ���ֵ�ɣ������򣬰���������С��ȡ��

    if (rb->rest < size) {//���ʣ�µı�1.25����󻺳�����СҪС�Ļ�
        size = (ssize_t) rb->rest;//��¼����ʣ������ֽ���
        if (r->request_body_in_single_buf) {//���ָ��ֻ��һ��buffer��Ҫ����Ԥ���ġ�
            size += preread;
        }

    } else {//���1.25����󻺳�����С����������POST���ݣ�������Ҳֻ��ȡ���POST�����ˡ�
        size = clcf->client_body_buffer_size;
        /* disable copying buffer for r->request_body_in_single_buf */
        b = NULL;
    }

    rb->buf = ngx_create_temp_buf(r->pool, size);//������ô����ʱ�ڴ�
    if (rb->buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl = ngx_alloc_chain_link(r->pool);//����һ�����ӱ�
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl->buf = rb->buf;//��¼һ�¸ղ�������ڴ棬�������ݾʹ�������ˡ�
    cl->next = NULL;//û����һ���ˡ�
    if (b && r->request_body_in_single_buf) {//���ָ��ֻ��һ��buffer��Ҫ����Ԥ����,�Ǿ���Ҫ��֮ǰ�����ݿ�������
        size = b->last - b->pos;
        ngx_memcpy(rb->buf->pos, b->pos, size);
        rb->buf->last += size;
        next = &rb->bufs;//����������ͷ����
    }

    *next = cl;//GOOD�����������������Ԥ�����ݣ��ҿ��ԷŶ��buffer,�������ڵڶ���λ�ã����������ڵ�һ��λ�á�
    if (r->request_body_in_file_only || r->request_body_in_single_buf) {
        rb->to_write = rb->bufs;//����һ�´�����Ҫд���λ�á����һ��buffer����ͷ��

    } else {//��������Ѿ������˵ڶ���λ�ã�Ҳ������Ԥ����������2��BUFFER���Ǿʹ��ڵڶ������棬����ͷ����
        rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;
    }
    r->read_event_handler = ngx_http_read_client_request_body_handler;//����Ϊ��ȡ�ͻ��˵��������ȡ��������ʵ�͵�������ģ�ֻ�ǽ����˳�ʱ�ж�

    return ngx_http_do_read_client_request_body(r);//���ϵ�ȥ��ʼ��ȡʣ��������
}


static void
ngx_http_read_client_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;
    if (r->connection->read->timedout) {//�Ϲ�أ���ʱ�жϡ�
        r->connection->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }
    rc = ngx_http_do_read_client_request_body(r);//����ȥ��ȡ������
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_finalize_request(r, rc);
    }
}


static ngx_int_t
ngx_http_do_read_client_request_body(ngx_http_request_t *r)
{//��ʼ��ȡʣ���POST���ݣ������r->request_body���棬��������ˣ��ص�post_handler����ʵ����ngx_http_upstream_init��
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;//�õ�������ӵ�ngx_connection_t�ṹ
    rb = r->request_body;//�õ����ݴ��λ��
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http read client request body");
    for ( ;; ) {
        for ( ;; ) {
            if (rb->buf->last == rb->buf->end) {//������ݻ����������ˡ�
                if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {//�����ˣ��ط�����������д���ļ�����ȥ�ɡ�
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
                rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;//������еڶ�������������д��ڶ���������д��һ����
                rb->buf->last = rb->buf->start;
            }
            size = rb->buf->end - rb->buf->last;//���������������ʣ���С
            if ((off_t) size > rb->rest) {//�������ˣ�size�͵�����Ҫ��ȡ�Ĵ�С������Ļ��Ͷ�ʣ���������С��
                size = (size_t) rb->rest;
            }
            n = c->recv(c, rb->buf->last, size);//ʹ�������ݡ�����ngx_unix_recv���͹�����ݣ����ı�epoll���ԡ�
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http client request body recv %z", n);

            if (n == NGX_AGAIN) {//û����
                break;
            }
            if (n == 0) {//����0��ʾ�ͻ��˹ر�������
                ngx_log_error(NGX_LOG_INFO, c->log, 0,"client closed prematurely connection");
            }
            if (n == 0 || n == NGX_ERROR) {
                c->error = 1;//���Ϊ���������ⲿ��ر����ӵġ�
                return NGX_HTTP_BAD_REQUEST;
            }
            rb->buf->last += n;//����n�ֽڡ�
            rb->rest -= n;//������ô���ֽ�Ҫ��ȡ
            r->request_length += n;//ͳ���ܴ����ֽ���
            if (rb->rest == 0) {
                break;
            }

            if (rb->buf->last < rb->buf->end) {
                break;//����϶��˰ɡ�
            }
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http client request body rest %O", rb->rest);
        if (rb->rest == 0) {
            break;//��ʣ���ˣ�ȫ�����ˡ�
        }

        if (!c->read->ready) {//������������ngx_unix_recv������Ϊû���㹻���ݿ��Զ�ȡ�ˣ������Ǿ���Ҫ����epool�ɶ������¼�����ȥ��
            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            ngx_add_timer(c->read, clcf->client_body_timeout);//�����ʱ�ɡ�
//���������õ��ɶ��¼�����У��пɶ��¼��ͻ����ngx_http_request_handler->r->read_event_handler = ngx_http_read_client_request_body_handler; 
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            return NGX_AGAIN;//����
        }
    }
//���ȫ�������ˣ��Ǿͻᵽ��������
    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }
    if (rb->temp_file || r->request_body_in_file_only) {//��д�ļ���д�ļ�
        /* save the last part */
        if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->in_file = 1;
        b->file_pos = 0;
        b->file_last = rb->temp_file->file.offset;
        b->file = &rb->temp_file->file;
        if (rb->bufs->next) {//�еڶ����ͷŵڶ���
            rb->bufs->next->buf = b;

        } else {
            rb->bufs->buf = b;//����ŵ�һ��������
        }
    }

    if (r->request_body_in_file_only && rb->bufs->next) {//���POST���ݱ��������ļ��У�������2����������������ɶ��˼?
        rb->bufs = rb->bufs->next;//ָ��ڶ�������������ʵ����������ļ���Ҳ����bufsָ���ļ�
    }

    rb->post_handler(r);

    return NGX_OK;
}


static ngx_int_t
ngx_http_write_request_body(ngx_http_request_t *r, ngx_chain_t *body)
{
    ssize_t                    n;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    rb = r->request_body;

    if (rb->temp_file == NULL) {
        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return NGX_ERROR;
        }

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = clcf->client_body_temp_path;
        tf->pool = r->pool;
        tf->warn = "a client request body is buffered to a temporary file";
        tf->log_level = r->request_body_file_log_level;
        tf->persistent = r->request_body_in_persistent_file;
        tf->clean = r->request_body_in_clean_file;

        if (r->request_body_file_group_access) {
            tf->access = 0660;
        }

        rb->temp_file = tf;
    }

    n = ngx_write_chain_to_temp_file(rb->temp_file, body);
    /* TODO: n == 0 or not complete and level event */
    if (n == NGX_ERROR) {
        return NGX_ERROR;
    }

    rb->temp_file->offset += n;

    return NGX_OK;
}


ngx_int_t
ngx_http_discard_request_body(ngx_http_request_t *r)
{//ɾ���ͻ������Ӷ��¼���������ԣ���ȡ�ͻ���BODY��Ȼ�󶪵��������������BODY�ˣ�lingering_close=0.
    ssize_t       size;
    ngx_event_t  *rev;

    if (r != r->main || r->discard_body) {
        return NGX_OK;//������������󣬻����Ѿ�����BODY�ˣ�ֱ�ӷ���
    }
	//�����Ҫ����HTTP 1.1�� 100-continue�ķ����������ngx_unix_send���ͷ�����ȥ
    if (ngx_http_test_expect(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rev = r->connection->read;//�õ����ӵĶ��¼��ṹ
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http set discard body");
    if (rev->timer_set) {//���ڲ���Ҫ�ͻ��˷��͵�body�ˣ����ɾ������ʱ��ʱ������care��ȡ�¼��ˡ�
        ngx_del_timer(rev);
    }

    if (r->headers_in.content_length_n <= 0 || r->request_body) {
        return NGX_OK;//��������峤��Ϊ0�������������request_body��˵������������Ѿ���ȡ�ˣ���Ҳ���ˡ����������� �
    }

    size = r->header_in->last - r->header_in->pos;//���������ݿ϶���body�ˡ�
    if (size) {//�Ѿ���С��Ԥ����һЩ���ݣ���ô���Ԥ�������ݻ�����iȫ����body���Ǿ��ƶ�һ��pos�Լ�����һ��content_length_n��
    //��ƭ��˵�ҵ�content_length_nû��ô�࣬ʵ�������Ѿ���ȡ�ˡ�
        if (r->headers_in.content_length_n > size) {
            r->header_in->pos += size;
            r->headers_in.content_length_n -= size;
        } else {//����body�Ѿ������ˣ�Ԥ�������ݱ�content_length_n�����ˣ��ǿ϶������ˣ�
        //�Ǿ��ƶ�pos,Ȼ��content_length_n����Ϊ0��ƭ��˵û���ݶ��ˣ��ͻ���û��������
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;
            return NGX_OK;
        }
    }
	//��������ͻ������ӵĶ�ȡ�¼��������Ϊ���¡�
    r->read_event_handler = ngx_http_discarded_request_body_handler;
	//�������Ϊ0��ɶ��û��
    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	//����������϶�ȡ�ͻ������ӵ����ݣ�Ȼ�󶪵����������NGX_OK��
	//��ʾ������ӷ��͵����ݵ�ͷ�ˣ����緢����FIN��������û��BODY�ˡ��Ǿ�����һ��
    if (ngx_http_read_discarded_request_body(r) == NGX_OK) {
//����һ������: ����ͻ������ڷ������ݣ������ݻ�û�е������ˣ�����˾ͽ����ӹص��ˡ���ô���ͻ��˷��͵����ݻ��յ�RST��,���Ѻá�
//��������֪���Է����ᷢ�������ˣ���ô��������־�ɡ�ֱ�ӹرտͻ��˵����ӡ�������ngx_http_finalize_connection����Ͳ����ӳٹر��ˡ�
        r->lingering_close = 0;

    } else {
        r->count++;
        r->discard_body = 1;//���λ�û�ж�������body�����������־�󣬱������´ν����ͻ��ڿ�ͷֱ�ӷ��ء���Ϊ�Ѿ����ù���ص������ˡ�
    }

    return NGX_OK;
}


void
ngx_http_discarded_request_body_handler(ngx_http_request_t *r)
{//�������ͻ��˵���������
    ngx_int_t                  rc;
    ngx_msec_t                 timer;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rev = c->read;

    if (rev->timedout) {
        c->timedout = 1;
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->lingering_time) {
        timer = (ngx_msec_t) (r->lingering_time - ngx_time());

        if (timer <= 0) {
            r->discard_body = 0;
            r->lingering_close = 0;
            ngx_http_finalize_request(r, NGX_ERROR);
            return;
        }

    } else {
        timer = 0;
    }

    rc = ngx_http_read_discarded_request_body(r);

    if (rc == NGX_OK) {
        r->discard_body = 0;
        r->lingering_close = 0;
        ngx_http_finalize_request(r, NGX_DONE);
        return;
    }

    /* rc == NGX_AGAIN */

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (timer) {

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        timer *= 1000;

        if (timer > clcf->lingering_timeout) {
            timer = clcf->lingering_timeout;
        }

        ngx_add_timer(rev, timer);
    }
}


static ngx_int_t
ngx_http_read_discarded_request_body(ngx_http_request_t *r)
{//����������϶�ȡ�ͻ������ӵ����ݣ�Ȼ�󶪵�������������: Ϊɶ���ɴ಻��ȡ�أ������������ˡ�
//���У��������ȡ���Ǿ�hold��TCPЭ��ջ���棬�Ƕ�ϵͳ�ڴ���ѹ����
//����Ҫ����: �ͻ��˽��޷��������ݣ���Ϊ���ǵ�ӵ������windowΪ0��
    size_t   size;
    ssize_t  n;
    u_char   buffer[NGX_HTTP_DISCARD_BUFFER_SIZE];

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,  "http read discarded body");
    for ( ;; ) {
        if (r->headers_in.content_length_n == 0) {
			//����ľ��ɾ�����ӵĶ��¼�ע�ᣬ����ע���¼��ˡ�
            r->read_event_handler = ngx_http_block_reading;
            return NGX_OK;
        }

        if (!r->connection->read->ready) {
            return NGX_AGAIN;
        }
//ȡ����С�����4096��С�Ŀ�һ�δεĶ�ȡ��Ȼ������Ķ���
        size = (r->headers_in.content_length_n > NGX_HTTP_DISCARD_BUFFER_SIZE) ? NGX_HTTP_DISCARD_BUFFER_SIZE:(size_t) r->headers_in.content_length_n;
        n = r->connection->recv(r->connection, buffer, size);
        if (n == NGX_ERROR) {
            r->connection->error = 1;
            return NGX_OK;
        }
        if (n == NGX_AGAIN) {
            return NGX_AGAIN;
        }
        if (n == 0) {
            return NGX_OK;
        }
		//����content_length_n��С����������Ϊû��ô��body��ʵ�����Ƕ�ȡ���ˡ�
        r->headers_in.content_length_n -= n;
    }
}


static ngx_int_t
ngx_http_test_expect(ngx_http_request_t *r)
{//�����Ҫ����100-continue�ķ����������ngx_unix_send���ͷ�����ȥ
    ngx_int_t   n;
    ngx_str_t  *expect;

    if (r->expect_tested
        || r->headers_in.expect == NULL
        || r->http_version < NGX_HTTP_VERSION_11)
    {
        return NGX_OK;
    }
    r->expect_tested = 1;
    expect = &r->headers_in.expect->value;
    if (expect->len != sizeof("100-continue") - 1 || ngx_strncasecmp(expect->data, (u_char *) "100-continue", sizeof("100-continue") - 1) != 0)
    {
        return NGX_OK;
    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "send 100 Continue");
    n = r->connection->send(r->connection, (u_char *) "HTTP/1.1 100 Continue" CRLF CRLF, sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1);

    if (n == sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1) {
        return NGX_OK;
    }
    /* we assume that such small packet should be send successfully */
    return NGX_ERROR;
}
