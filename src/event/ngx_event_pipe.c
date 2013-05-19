
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_pipe.h>


static ngx_int_t ngx_event_pipe_read_upstream(ngx_event_pipe_t *p);
static ngx_int_t ngx_event_pipe_write_to_downstream(ngx_event_pipe_t *p);

static ngx_int_t ngx_event_pipe_write_chain_to_temp_file(ngx_event_pipe_t *p);
static ngx_inline void ngx_event_pipe_remove_shadow_links(ngx_buf_t *buf);
static ngx_inline void ngx_event_pipe_free_shadow_raw_buf(ngx_chain_t **free,
                                                          ngx_buf_t *buf);
static ngx_int_t ngx_event_pipe_drain_chains(ngx_event_pipe_t *p);


ngx_int_t ngx_event_pipe(ngx_event_pipe_t *p, ngx_int_t do_write)
{//����buffering��ʱ��ʹ��event_pipe�������ݵ�ת��������ngx_event_pipe_write_to*������ȡ���ݣ����߷������ݸ��ͻ��ˡ�
//ngx_event_pipe��upstream��Ӧ���ͻؿͻ��ˡ�do_write�����Ƿ�Ҫ���ͻ��˷��ͣ�д���ݡ�
//��������ˣ���ô���ȷ����ͻ��ˣ��ٶ�upstream���ݣ���Ȼ�������ȡ�����ݣ�Ҳ���������ġ�
    u_int         flags;
    ngx_int_t     rc;
    ngx_event_t  *rev, *wev;

//���forѭ���ǲ��ϵ���ngx_event_pipe_read_upstream��ȡ�ͻ������ݣ�Ȼ�����ngx_event_pipe_write_to_downstream
    for ( ;; ) {
        if (do_write) {
            p->log->action = "sending to client";
            rc = ngx_event_pipe_write_to_downstream(p);
            if (rc == NGX_ABORT) {
                return NGX_ABORT;
            }
            if (rc == NGX_BUSY) {
                return NGX_OK;
            }
        }
        p->read = 0;
        p->upstream_blocked = 0;
        p->log->action = "reading upstream";
		//��upstream��ȡ���ݵ�chain���������棬Ȼ����������ĵ���input_filter����Э��Ľ���������HTTP��������p->in��p->last_in���������档
        if (ngx_event_pipe_read_upstream(p) == NGX_ABORT) {
            return NGX_ABORT;
        }
		//upstream_blocked����ngx_event_pipe_read_upstream�������õı���,�����Ƿ��������Ѿ���upstream��ȡ�ˡ�
        if (!p->read && !p->upstream_blocked) {
            break;
        }
        do_write = 1;//��Ҫд����Ϊ����ζ�����һЩ����
    }
	
//�����Ǵ����Ƿ���Ҫ���ö�ʱ��������ɾ����д�¼���epoll��
    if (p->upstream->fd != -1) {//������php�ȵ�����fd����Ч�ģ���ע���д�¼���
        rev = p->upstream->read;//�õ�������ӵĶ�д�¼��ṹ������䷢���˴�����ô�����д�¼�ע��ɾ���������򱣴�ԭ����
        flags = (rev->eof || rev->error) ? NGX_CLOSE_EVENT : 0;
        if (ngx_handle_read_event(rev, flags) != NGX_OK) {
            return NGX_ABORT;//�����Ƿ���Ҫ���������ɾ����д�¼�ע�ᡣ
        }
        if (rev->active && !rev->ready) {//û�ж�д�����ˣ��Ǿ�����һ������ʱ��ʱ��
            ngx_add_timer(rev, p->read_timeout);
        } else if (rev->timer_set) {
            ngx_del_timer(rev);
        }
    }
    if (p->downstream->fd != -1 && p->downstream->data == p->output_ctx) {
        wev = p->downstream->write;//�Կͻ��˵����ӣ�ע���д�¼������Ŀ�д
        if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
            return NGX_ABORT;
        }
        if (!wev->delayed) {
            if (wev->active && !wev->ready) {//ͬ����ע��һ�³�ʱ��
                ngx_add_timer(wev, p->send_timeout);
            } else if (wev->timer_set) {
                ngx_del_timer(wev);
            }
        }
    }
    return NGX_OK;
}
/*
1.��preread_bufs��free_raw_bufs����ngx_create_temp_bufѰ��һ����еĻ򲿷ֿ��е��ڴ棻
2.����p->upstream->recv_chain==ngx_readv_chain����writev�ķ�ʽ��ȡFCGI������,���chain��
3.��������buf�����˵�chain�ڵ����input_filter(ngx_http_fastcgi_input_filter)����upstreamЭ�����������FCGIЭ�飬������Ľ������p->in���棻
4.����û���������buffer�ڵ㣬����free_raw_bufs�Դ��´ν���ʱ�Ӻ������׷�ӡ�
5.��Ȼ�ˣ�����Զ˷���������FIN�ˣ��Ǿ�ֱ�ӵ���input_filter����free_raw_bufs������ݡ�
*/
static ngx_int_t ngx_event_pipe_read_upstream(ngx_event_pipe_t *p)
{//ngx_event_pipe���������ȡ��˵����ݡ�
    ssize_t       n, size;
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, *ln;

    if (p->upstream_eof || p->upstream_error || p->upstream_done) {
        return NGX_OK;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe read upstream: %d", p->upstream->read->ready);

    for ( ;; ) {
        if (p->upstream_eof || p->upstream_error || p->upstream_done) {
            break;//״̬�жϡ�
        }
		//���û��Ԥ�����ݣ����Ҹ�upstream�����ӻ�û��read���ǾͿ����˳��ˣ���Ϊû���ݿɶ���
        if (p->preread_bufs == NULL && !p->upstream->read->ready) {
            break;
        }
		//����������if-else�͸�һ������: Ѱ��һ����е��ڴ滺���������������Ŷ�ȡ������upstream�����ݡ�
		//���preread_bufs��Ϊ�գ�������֮�����򿴿�free_raw_bufs��û�У���������һ��
        if (p->preread_bufs) {//���Ԥ�������еĻ��������һ�ν�����������δ�ɶ�������֮ǰ������һ����body���Ǿ��ȴ��������body�ٽ��ж�ȡ��
            /* use the pre-read bufs if they exist */
            chain = p->preread_bufs;//�Ǿͽ�������������������,����������Ŷ�������ݡ������preread_bufs,
            p->preread_bufs = NULL;
            n = p->preread_size;
            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,  "pipe preread: %z", n);
            if (n) {
                p->read = 1;//�������ݡ�
            }
        } else {//����preread_bufsΪ�գ�û���ˡ�
#if (NGX_HAVE_KQUEUE)//�м�ɾ���ˡ������ǡ�
#endif
            if (p->free_raw_bufs) {
                /* use the free bufs if they exist */
                chain = p->free_raw_bufs;
                if (p->single_buf) {//���������NGX_USE_AIO_EVENT��־�� the posted aio operation may currupt a shadow buffer
                    p->free_raw_bufs = p->free_raw_bufs->next;
                    chain->next = NULL;
                } else {//�������AIO����ô�����ö���ڴ�һ����readv��ȡ�ġ�
                    p->free_raw_bufs = NULL;
                }
            } else if (p->allocated < p->bufs.num) {
            //���û�г���fastcgi_buffers��ָ������ƣ���ô����һ���ڴ�ɡ���Ϊ����û�п����ڴ��ˡ�
                /* allocate a new buf if it's still allowed */
			//����һ��ngx_buf_t�Լ�size��С�����ݡ������洢��FCGI��ȡ�����ݡ�
                b = ngx_create_temp_buf(p->pool, p->bufs.size);
                if (b == NULL) {
                    return NGX_ABORT;
                }
                p->allocated++;
                chain = ngx_alloc_chain_link(p->pool);//����һ������ṹ��ָ������������buf,���buf �Ƚϴ�ġ���ʮK���ϡ�
                if (chain == NULL) {
                    return NGX_ABORT;
                }
                chain->buf = b;
                chain->next = NULL;
            } else if (!p->cacheable && p->downstream->data == p->output_ctx && p->downstream->write->ready && !p->downstream->write->delayed) {
			//�������˵��û�������ڴ��ˣ�������������ûҪ������ȱ�����cache������ǿ��԰ɵ�ǰ�����ݷ��͸��ͻ����ˡ�����ѭ����
                /*
                 * if the bufs are not needed to be saved in a cache and
                 * a downstream is ready then write the bufs to a downstream
                 */
                p->upstream_blocked = 1;//����Ѿ���ȡ�����ݣ�����write�ˡ�
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe downstream ready");
                break;
            } else if (p->cacheable || p->temp_file->offset < p->max_temp_file_size)
            {//���뻺�棬���ҵ�ǰ�Ļ����ļ���λ�ƣ����Ǵ�СС�ڿ�����Ĵ�С����good������д���ļ��ˡ�
                /*
                 * if it is allowed, then save some bufs from r->in
                 * to a temporary file, and add them to a r->out chain
                 */
//���潫r->in������д����ʱ�ļ�
                rc = ngx_event_pipe_write_chain_to_temp_file(p);
                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,  "pipe temp offset: %O", p->temp_file->offset);
                if (rc == NGX_BUSY) {
                    break;
                }
                if (rc == NGX_AGAIN) {
                    if (ngx_event_flags & NGX_USE_LEVEL_EVENT && p->upstream->read->active && p->upstream->read->ready){
                        if (ngx_del_event(p->upstream->read, NGX_READ_EVENT, 0) == NGX_ERROR) {
                            return NGX_ABORT;
                        }
                    }
                }
                if (rc != NGX_OK) {
                    return rc;
                }
                chain = p->free_raw_bufs;
                if (p->single_buf) {
                    p->free_raw_bufs = p->free_raw_bufs->next;
                    chain->next = NULL;
                } else {
                    p->free_raw_bufs = NULL;
                }
            } else {//û�취�ˡ�
                /* there are no bufs to read in */
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0,  "no pipe bufs to read in");
                break;
            }
			//������϶����ҵ����е�buf�ˣ�chainָ��֮�ˡ���˯��������û���ˡ�
			//ngx_readv_chain .����readv���ϵĶ�ȡ���ӵ����ݡ�����chain����������
			//�����chain�ǲ���ֻ��һ��? ��next��ԱΪ���أ���һ�������free_raw_bufs��Ϊ�գ�
			//����Ļ�ȡ����bufֻҪû��ʹ��AIO�Ļ����Ϳ����ж��buffer����ġ�
            n = p->upstream->recv_chain(p->upstream, chain);

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe recv chain: %z", n);
            if (p->free_raw_bufs) {//free_raw_bufs��Ϊ�գ��Ǿͽ�chainָ������ŵ�free_raw_bufsͷ����
                chain->next = p->free_raw_bufs;
            }
            p->free_raw_bufs = chain;//����ͷ��
            if (n == NGX_ERROR) {
                p->upstream_error = 1;
                return NGX_ERROR;
            }
            if (n == NGX_AGAIN) {
                if (p->single_buf) {
                    ngx_event_pipe_remove_shadow_links(chain->buf);
                }
                break;
            }
            p->read = 1;
            if (n == 0) {
                p->upstream_eof = 1;//û�ж������ݣ��϶�upstream������FIN�����ǾͶ�ȡ����ˡ�
                break;
            }
        }//������forѭ���տ�ʼ��if (p->preread_bufs) {���������Ѱ��һ�����еĻ�������Ȼ���ȡ�������chain�������ġ�
//��ȡ�����ݣ�����Ҫ����FCGIЭ������������ˡ�
        p->read_length += n;
        cl = chain;//chain�Ѿ��������ͷ���ˣ�����free_raw_bufs������������ÿ��ȡ�
        p->free_raw_bufs = NULL;

        while (cl && n > 0) {//��������������ݲ��ҳ��Ȳ�Ϊ0��Ҳ������εĻ�û�д����ꡣ�����֮ǰ������һ����������?
        //����ģ����֮ǰԤ�������ݣ���ô����Ĵ�if���else�������ȥ�����Ǵ�ʱ��n�϶�����preread_bufs�ĳ���preread_size��
        //���֮ǰû��Ԥ�����ݣ���free_raw_bufs��Ϊ�գ���Ҳû��ϵ��free_raw_bufs��������ݿ϶��Ѿ������漸�д�����ˡ�

		//����ĺ�����c->buf����shadowָ���������������������нڵ��recycled,temporary,shadow��Ա�ÿա�
            ngx_event_pipe_remove_shadow_links(cl->buf);

            size = cl->buf->end - cl->buf->last;
            if (n >= size) {
                cl->buf->last = cl->buf->end;//������ȫ������,readv��������ݡ�
                /* STUB */ cl->buf->num = p->num++;//�ڼ���
				//FCGIΪngx_http_fastcgi_input_filter������Ϊngx_event_pipe_copy_input_filter �����������ض���ʽ����
                if (p->input_filter(p, cl->buf) == NGX_ERROR) {//����buffer�ĵ���Э��������
                //�����棬���cl->buf������ݽ���������DATA���ݣ���ôcl->buf->shadow��Աָ��һ������
                //ͨ��shadow��Ա��������������ÿ����Ա������ɢ��fcgi data���ݲ��֡�
                    return NGX_ABORT;
                }
                n -= size;
                ln = cl;
                cl = cl->next;//����������һ�飬���ͷ�����ڵ㡣
                ngx_free_chain(p->pool, ln);

            } else {//�������ڵ�Ŀ����ڴ���Ŀ����ʣ��Ҫ����ģ��ͽ�ʣ�µĴ�������
                cl->buf->last += n;//ɶ��˼�����õ���input_filter���𣬲��ǡ��������ģ����ʣ�µ�������ݻ�����������ǰ���cl�Ļ����С��
                n = 0;//�Ǿ��ȴ���������ô����: ���ͷ�cl�ˣ�ֻ���ƶ����С��Ȼ��n=0ʹѭ���˳���Ȼ�������漸�е�if (cl) {������Լ�⵽�������
 //�����������if����Ὣ���ln�������ݷ���free_raw_bufs��ͷ��������������ж��������? �����еġ�
            }
        }

        if (cl) {
	//������û������һ���ڴ����������ӷŵ�free_raw_bufs��ǰ�档ע�������޸���cl->buf->last�������Ķ������ݲ��Ḳ����Щ���ݵġ���ngx_readv_chain
            for (ln = cl; ln->next; ln = ln->next) { /* void */ }
            ln->next = p->free_raw_bufs;//�������NULL�������ʼ���ģ����ԣ���Ϊinput_filter���ܻὫ��Щû��data���ֵ�fcgi���ݰ������free_raw_bufsֱ�ӽ��и��á�
            p->free_raw_bufs = cl;//��������һ��ѭ����ʱ��Ҳ�������棬��ʹ��free_raw_bufs�ġ�
            //���ң����ѭ�������ˣ����������ٴ���һ�����β��û����������������ݡ�
        }
    }//forѭ��������

#if (NGX_DEBUG)
    for (cl = p->busy; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf busy s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = p->out; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf out  s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = p->in; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf in   s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = p->free_raw_bufs; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf free s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

#endif

    if ((p->upstream_eof || p->upstream_error) && p->free_raw_bufs) {//û�취�ˣ����쵽ͷ�ˣ����߳��ִ����ˣ����Դ���һ����鲻������buffer
        /* STUB */ p->free_raw_bufs->buf->num = p->num++;
		//������ݶ�ȡ����ˣ����ߺ�˳��������ˣ����ң�free_raw_bufs��Ϊ�գ����滹��һ�������ݣ�
		//��Ȼֻ������һ�顣�Ǿ͵���input_filter��������FCGIΪngx_http_fastcgi_input_filter ��ngx_http_fastcgi_handler�������õ�

		//���￼��һ�����: �������һ�������ˣ�û��������û��data���ݣ�����ngx_http_fastcgi_input_filter�����ngx_event_pipe_add_free_buf������
		//������ڴ����free_raw_bufs��ǰ�棬���Ǿ���֪�������һ�鲻�������ݲ��ֵ��ڴ����õ���free_raw_bufs����Ϊfree_raw_bufs��û���ü��ı䡣
		//���ԣ��Ͱ��Լ����滻���ˡ���������ᷢ����?
        if (p->input_filter(p, p->free_raw_bufs->buf) == NGX_ERROR) {
            return NGX_ABORT;
        }
        p->free_raw_bufs = p->free_raw_bufs->next;
        if (p->free_bufs && p->buf_to_file == NULL) {
            for (cl = p->free_raw_bufs; cl; cl = cl->next) {
                if (cl->buf->shadow == NULL) 
			//���shadow��Աָ���������buf������СFCGI���ݿ�buf��ָ���б����ΪNULL����˵�����bufû��data�������ͷ��ˡ�
                    ngx_pfree(p->pool, cl->buf->start);
                }
            }
        }
    }
    if (p->cacheable && p->in) {
        if (ngx_event_pipe_write_chain_to_temp_file(p) == NGX_ABORT) {
            return NGX_ABORT;
        }
    }
    return NGX_OK;
}


static ngx_int_t
ngx_event_pipe_write_to_downstream(ngx_event_pipe_t *p)
{//ngx_event_pipe��������������ݷ��͸��ͻ��ˣ������Ѿ�׼����p->out,p->in�����ˡ�
    u_char            *prev;
    size_t             bsize;
    ngx_int_t          rc;
    ngx_uint_t         flush, flushed, prev_last_shadow;
    ngx_chain_t       *out, **ll, *cl, file;
    ngx_connection_t  *downstream;

    downstream = p->downstream;
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,"pipe write downstream: %d", downstream->write->ready);
    flushed = 0;

    for ( ;; ) {
        if (p->downstream_error) {//����ͻ������ӳ����ˡ�drain=��ˮ������,
        //���upstream�������ģ���������ʽ���HTML���ݡ��������free_raw_bufs���档
            return ngx_event_pipe_drain_chains(p);
        }
        if (p->upstream_eof || p->upstream_error || p->upstream_done) {
//���upstream�������Ѿ��ر��ˣ���������ˣ����߷�������ˣ��ǾͿ��Է����ˡ�
            /* pass the p->out and p->in chains to the output filter */
            for (cl = p->busy; cl; cl = cl->next) {
                cl->buf->recycled = 0;
            }

            if (p->out) {//����д��������
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream flush out");
                for (cl = p->out; cl; cl = cl->next) {
                    cl->buf->recycled = 0;//����Ҫ�����ظ������ˣ���Ϊupstream_done�ˣ������ٸ��ҷ��������ˡ�
                }
				//���棬��Ϊp->out����������һ��鶼�ǽ������HTML���ݣ�����ֱ�ӵ���ngx_http_output_filter����HTML���ݷ��;����ˡ�
                rc = p->output_filter(p->output_ctx, p->out);
                if (rc == NGX_ERROR) {
                    p->downstream_error = 1;
                    return ngx_event_pipe_drain_chains(p);
                }
                p->out = NULL;
            }

            if (p->in) {//��outͬ���򵥵���ngx_http_output_filter�������filter���͹����С�
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream flush in");
                for (cl = p->in; cl; cl = cl->next) {
                    cl->buf->recycled = 0;//�Ѿ��������ˣ�����Ҫ������
                }
				//ע������ķ��Ͳ������writev�ˣ��ÿ�������������Ƿ���Ҫrecycled,�Ƿ������һ��ȡ�ngx_http_write_filter���ж�����ġ�
                rc = p->output_filter(p->output_ctx, p->in);//����ngx_http_output_filter���ͣ����һ����ngx_http_write_filter
                if (rc == NGX_ERROR) {
                    p->downstream_error = 1;
                    return ngx_event_pipe_drain_chains(p);
                }
                p->in = NULL;
            }
			//���Ҫ���棬�Ǿ�д�뵽�ļ�����ȥ��
            if (p->cacheable && p->buf_to_file) {
                file.buf = p->buf_to_file;
                file.next = NULL;
                if (ngx_write_chain_to_temp_file(p->temp_file, &file) == NGX_ERROR){
                    return NGX_ABORT;
                }
            }

            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream done");
            /* TODO: free unused bufs */
            p->downstream_done = 1;
            break;
        }

		//����upstream���ݻ�û�з�����ϡ�
        if (downstream->data != p->output_ctx || !downstream->write->ready || downstream->write->delayed) {
            break;
        }
        /* bsize is the size of the busy recycled bufs */
        prev = NULL;
        bsize = 0;
//���������Ҫbusy������ڷ��ͣ��Ѿ����ù�output_filter��buf��������һ����Щ���Ի����ظ����õ�buf
//������Щbuf����������ע�����ﲻ�Ǽ���busy�л��ж�������û������writev��ȥ�����������ܹ����������
        for (cl = p->busy; cl; cl = cl->next) {
            if (cl->buf->recycled) {
                if (prev == cl->buf->start) {
                    continue;
                }
                bsize += cl->buf->end - cl->buf->start;
                prev = cl->buf->start;
            }
        }
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write busy: %uz", bsize);
        out = NULL;
		//busy_sizeΪfastcgi_busy_buffers_size ָ�����õĴ�С��ָ�������͵�busy״̬���ڴ��ܴ�С��
		//������������С��nginx�᳢��ȥ�����µ����ݲ�������Щbusy״̬��buf��
        if (bsize >= (size_t) p->busy_size) {
            flush = 1;//���busy������������ݺܶ��ˣ�����fastcgi_busy_buffers_size ָ��Ǿ͸Ͻ�ȥ���ͣ����հɣ���Ȼfree_raw_bufs����û���û����ˡ�
            goto flush;
        }

        flush = 0;
        ll = NULL;
        prev_last_shadow = 1;//�����һ���ڵ��ǲ���������һ��FCGI buffer�����һ�����ݽڵ㡣
//����p->out,p->in�����δ�������ݣ������Ƿŵ�out������棬ע�����﷢�͵����ݲ�����busy_size��Ϊ���������ˡ�
        for ( ;; ) {
//ѭ�������ѭ������ֹ�����Ǿ��ܻ�ü���HTML���ݽڵ㣬�������ǿ�Խ��1�����ϵ�FCGI���ݿ�Ĳ������һ�����last_shadow������
            if (p->out) {//buf��tempfile�����ݻ�ŵ�out���档
                cl = p->out;
                if (cl->buf->recycled && bsize + cl->buf->last - cl->buf->pos > p->busy_size) {
                    flush = 1;//�ж��Ƿ񳬹�busy_size
                    break;
                }
                p->out = p->out->next;
                ngx_event_pipe_free_shadow_raw_buf(&p->free_raw_bufs, cl->buf);
            } else if (!p->cacheable && p->in) {
                cl = p->in;
                ngx_log_debug3(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write buf ls:%d %p %z", cl->buf->last_shadow, cl->buf->pos, cl->buf->last - cl->buf->pos);
				//
                if (cl->buf->recycled && cl->buf->last_shadow && bsize + cl->buf->last - cl->buf->pos > p->busy_size)  {
					//1.������in��������ݣ��������Ҫ����;
					//2.��������ĳһ���FCGI buf�����һ����Чhtml���ݽڵ㣻
					//3.���ҵ�ǰ��û���͵Ĵ�С����busy_size, �Ǿ���Ҫ����һ���ˣ���Ϊ������buffer����
                    if (!prev_last_shadow) {
		//���ǰ���һ�鲻��ĳ����FCGI buffer�����һ�����ݿ飬�Ǿͽ���ǰ������out�ĺ��棬Ȼ���˳�ѭ��ȥflash
		//ʲô��˼�أ�����˵�������ǰ����鲻�ᵼ��out��������һ���ڵ㣬�������ڶ����ڵ�������һ��FCGI���ڴ�Ľ�β��
		//��ʵ��i���˸��Ż�,��nginx����һ���ķ��͡�
                        p->in = p->in->next;
                        cl->next = NULL;
                        if (out) {
                            *ll = cl;
                        } else {
                            out = cl;
                        }
                    }
                    flush = 1;//�����˴�С�����һ�´�������Ҫ�������͵ġ������������û���Ӷ������ã���Ϊ���治��ô�жϡ�
                    break;//ֹͣ���������ڴ�飬��Ϊ�����Ѿ�����busy_size�ˡ�
                }
                prev_last_shadow = cl->buf->last_shadow;
                p->in = p->in->next;
            } else {
                break;//����û�������ˣ���û�취�ˣ����ɡ�����һ������϶���last_shadowΪ1�ġ�������ѽ����ġ�
            }
//clָ��ǰ��Ҫ��������ݣ�����cl = p->out����cl = p->in;
//����ͽ�����ڴ����outָ�����������llָ�����һ���nextָ���ַ��
            if (cl->buf->recycled) {//������buf����Ҫ�������õģ���ͳ�����С
                bsize += cl->buf->last - cl->buf->pos;
            }
            cl->next = NULL;
            if (out) {
                *ll = cl;
            } else {
                out = cl;//ָ���һ������
            }
            ll = &cl->next;
        }
//�������outָ��ָ��һ������������������Ǵ�p->out,p->in����Ҫ���͵����ݡ�
    flush:
//���潫outָ��ָ����ڴ����output_filter������filter���̡�
        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write: out:%p, f:%d", out, flush);
        if (out == NULL) {
            if (!flush) {
                break;
            }
            /* a workaround for AIO */
            if (flushed++ > 10) {
                return NGX_BUSY;
            }
        }
        rc = p->output_filter(p->output_ctx, out);//�򵥵���ngx_http_output_filter�������filter���͹����С�
        if (rc == NGX_ERROR) {
            p->downstream_error = 1;
            return ngx_event_pipe_drain_chains(p);
        }
		//��out�������ƶ���busy��busy�з�����ɵ��ƶ���free
        ngx_chain_update_chains(&p->free, &p->busy, &out, p->tag);
        for (cl = p->free; cl; cl = cl->next) {
            if (cl->buf->temp_file) {
                if (p->cacheable || !p->cyclic_temp_file) {
                    continue;
                }
                /* reset p->temp_offset if all bufs had been sent */
                if (cl->buf->file_last == p->temp_file->offset) {
                    p->temp_file->offset = 0;
                }
            }
            /* TODO: free buf if p->free_bufs && upstream done */
            /* add the free shadow raw buf to p->free_raw_bufs */
            if (cl->buf->last_shadow) {
	//ǰ��˵���ˣ��������ڴ�������������FCGI���ڴ�����һ��data�ڵ㣬���ͷ�����FCGI buffer��
	//��last_shadowΪ1��ʱ��buf->shadowʵ����ָ���������FCGI��buf�ġ�Ҳ����ԭʼbuf������buf���Ǹ�Ӱ�ӣ�����ָ��ĳ��ԭʼ��buf.
                if (ngx_event_pipe_add_free_buf(p, cl->buf->shadow) != NGX_OK) {
                    return NGX_ABORT;
                }
                cl->buf->last_shadow = 0;
            }
            cl->buf->shadow = NULL;
        }
    }
    return NGX_OK;
}


static ngx_int_t
ngx_event_pipe_write_chain_to_temp_file(ngx_event_pipe_t *p)
{
    ssize_t       size, bsize;
    ngx_buf_t    *b;
    ngx_chain_t  *cl, *tl, *next, *out, **ll, **last_free, fl;

    if (p->buf_to_file) {
        fl.buf = p->buf_to_file;
        fl.next = p->in;
        out = &fl;

    } else {
        out = p->in;
    }

    if (!p->cacheable) {

        size = 0;
        cl = out;
        ll = NULL;

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe offset: %O", p->temp_file->offset);

        do {
            bsize = cl->buf->last - cl->buf->pos;
            ngx_log_debug3(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe buf %p, pos %p, size: %z", cl->buf->start, cl->buf->pos, bsize);
            if ((size + bsize > p->temp_file_write_size)
               || (p->temp_file->offset + size + bsize > p->max_temp_file_size))
            {
                break;
            }

            size += bsize;
            ll = &cl->next;
            cl = cl->next;
        } while (cl);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "size: %z", size);

        if (ll == NULL) {
            return NGX_BUSY;
        }

        if (cl) {
           p->in = cl;
           *ll = NULL;

        } else {
           p->in = NULL;
           p->last_in = &p->in;
        }

    } else {
        p->in = NULL;
        p->last_in = &p->in;
    }

    if (ngx_write_chain_to_temp_file(p->temp_file, out) == NGX_ERROR) {
        return NGX_ABORT;
    }

    for (last_free = &p->free_raw_bufs;
         *last_free != NULL;
         last_free = &(*last_free)->next)
    {
        /* void */
    }

    if (p->buf_to_file) {
        p->temp_file->offset = p->buf_to_file->last - p->buf_to_file->pos;
        p->buf_to_file = NULL;
        out = out->next;
    }

    for (cl = out; cl; cl = next) {
        next = cl->next;
        cl->next = NULL;

        b = cl->buf;
        b->file = &p->temp_file->file;
        b->file_pos = p->temp_file->offset;
        p->temp_file->offset += b->last - b->pos;
        b->file_last = p->temp_file->offset;

        b->in_file = 1;
        b->temp_file = 1;

        if (p->out) {
            *p->last_out = cl;
        } else {
            p->out = cl;
        }
        p->last_out = &cl->next;

        if (b->last_shadow) {

            tl = ngx_alloc_chain_link(p->pool);
            if (tl == NULL) {
                return NGX_ABORT;
            }

            tl->buf = b->shadow;
            tl->next = NULL;

            *last_free = tl;
            last_free = &tl->next;

            b->shadow->pos = b->shadow->start;
            b->shadow->last = b->shadow->start;

            ngx_event_pipe_remove_shadow_links(b->shadow);
        }
    }

    return NGX_OK;
}


/* the copy input filter */
ngx_int_t ngx_event_pipe_copy_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf)
{
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }
    if (p->free) {
        cl = p->free;
        b = cl->buf;
        p->free = cl->next;
        ngx_free_chain(p->pool, cl);
    } else {
        b = ngx_alloc_buf(p->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
    }
    ngx_memcpy(b, buf, sizeof(ngx_buf_t));
    b->shadow = buf;
    b->tag = p->tag;
    b->last_shadow = 1;
    b->recycled = 1;
    buf->shadow = b;

    cl = ngx_alloc_chain_link(p->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf #%d", b->num);

    if (p->in) {
        *p->last_in = cl;
    } else {
        p->in = cl;
    }
    p->last_in = &cl->next;

    return NGX_OK;
}


static ngx_inline void
ngx_event_pipe_remove_shadow_links(ngx_buf_t *buf)
{//ɾ�����ݵ�shadow���Լ�recycled����Ϊ0����ʾ����Ҫѭ�����ã�����ʵ����buffering����
//��Ϊngx_http_write_filter���������ж������recycled��־���ͻ����������ݷ��ͳ�ȥ��
//������ｫ��Щ��־��գ���ngx_http_write_filter����ͻᾡ������ġ�
    ngx_buf_t  *b, *next;

    b = buf->shadow;//���shadowָ�����buf�����FCGI���ݵĵ�һ�����ݽڵ�
    if (b == NULL) {
        return;
    }
    while (!b->last_shadow) {//����������һ�����ݽڵ㣬�������������
        next = b->shadow;
        b->temporary = 0;
        b->recycled = 0;//���Ϊ���յ� �
        b->shadow = NULL;//��shadow��Ա�ÿա�
        b = next;
    }

    b->temporary = 0;
    b->recycled = 0;
    b->last_shadow = 0;
    b->shadow = NULL;
    buf->shadow = NULL;
}


static ngx_inline void
ngx_event_pipe_free_shadow_raw_buf(ngx_chain_t **free, ngx_buf_t *buf)
{
    ngx_buf_t    *s;
    ngx_chain_t  *cl, **ll;

    if (buf->shadow == NULL) {
        return;
    }
	//���治�ϵ����ŵ�ǰ��buf�����ߣ�ֱ���ߵ��˱���FCGI�����ݿ�����һ���ڵ����ݿ顣
    for (s = buf->shadow; !s->last_shadow; s = s->shadow) { /* void */ }

    ll = free;
    for (cl = *free; cl; cl = cl->next) {
        if (cl->buf == s) {//�����һ��Ļ�
            *ll = cl->next;
            break;
        }
        if (cl->buf->shadow) {
            break;
        }

        ll = &cl->next;
    }
}


ngx_int_t
ngx_event_pipe_add_free_buf(ngx_event_pipe_t *p, ngx_buf_t *b)
{//��������b��������ݿ����free_raw_bufs�Ŀ�ͷ���ߵڶ���λ�á�bΪ�ϲ����û���˵����ݿ顣
    ngx_chain_t  *cl;
//���ﲻ�����b�͵���free_raw_bufs->buf�������
    cl = ngx_alloc_chain_link(p->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    b->pos = b->start;//�ÿ���������
    b->last = b->start;
    b->shadow = NULL;

    cl->buf = b;

    if (p->free_raw_bufs == NULL) {
        p->free_raw_bufs = cl;
        cl->next = NULL;
        return NGX_OK;
    }
	//�������ע�ͣ���˼�ǣ������ǰ���free_raw_bufs��û�����ݣ��ǾͰɵ�ǰ������ݷ���ͷ�����С�
	//���������ǰfree_raw_bufs�����ݣ��Ǿ͵÷ŵ�������ˡ�Ϊʲô����������?���磬��ȡһЩ���ݺ󣬻�ʣ��һ��β�ʹ����free_raw_bufs��Ȼ��ʼ���ͻ���д����
	//д�����ȻҪ��û�õ�buffer���뵽�����������������ngx_event_pipe_write_to_downstream�������ġ����߸ɴ���ngx_event_pipe_drain_chains��������
	//��Ϊ���������inpupt_filter��������Ǵ����ݿ鿪ʼ����Ȼ�󵽺���ģ�
	//�����ڵ���input_filter֮ǰ�ǻὫfree_raw_bufs�ÿյġ�Ӧ���������ط�Ҳ�е��á�
    if (p->free_raw_bufs->buf->pos == p->free_raw_bufs->buf->last) {
        /* add the free buf to the list start */
        cl->next = p->free_raw_bufs;
        p->free_raw_bufs = cl;
        return NGX_OK;
    }
    /* the first free buf is partialy filled, thus add the free buf after it */
    cl->next = p->free_raw_bufs->next;
    p->free_raw_bufs->next = cl;
    return NGX_OK;
}


static ngx_int_t
ngx_event_pipe_drain_chains(ngx_event_pipe_t *p)
{//����p->in/out/busy������������������FCGI���ݿ��ͷţ����뵽free_raw_bufs�м�ȥ��Ҳ���ǣ����upstream�������ģ���������ʽ���HTML���ݡ�
    ngx_chain_t  *cl, *tl;

    for ( ;; ) {
        if (p->busy) {
            cl = p->busy;
            p->busy = NULL;
        } else if (p->out) {
            cl = p->out;
            p->out = NULL;
        } else if (p->in) {
            cl = p->in;
            p->in = NULL;
        } else {
            return NGX_OK;
        }
		//�ҵ���Ӧ������
        while (cl) {/*Ҫ֪��������cl���棬����p->in�������Щngx_buf_t�ṹ��ָ��������ڴ�ʵ��������
        ngx_event_pipe_read_upstream�����input_filter����Э�������ʱ������Ϊ���ӿͻ��˶�ȡ����ʱ��buf���õģ�Ҳ������ν��Ӱ�ӡ�
		Ȼ����Ȼp->inָ������������кܶ�ܶ���ڵ㣬ÿ���ڵ����һ��HTML���룬�������ǲ����Ƕ�ռһ���ڴ�ģ����ǿ��ܹ���ģ�
		����һ����buffer��������3��FCGI��STDOUT���ݰ�������data���֣���ô������3��b�Ľڵ����ӵ�p->in��ĩβ�����ǵ�shadow��Ա
		�ֱ�ָ����һ���ڵ㣬���һ���ڵ��ָ���������Ĵ��ڴ�ṹ��������ngx_http_fastcgi_input_filterʵ�֡�
        */
            if (cl->buf->last_shadow) {//������ĳ����FCGI���ݿ�����һ���ڵ㣬�ͷ�ֻ��Ȼ�������һ����������ĳ��Сhtml ���ݿ顣
                if (ngx_event_pipe_add_free_buf(p, cl->buf->shadow) != NGX_OK) {
                    return NGX_ABORT;
                }
                cl->buf->last_shadow = 0;
            }

            cl->buf->shadow = NULL;
            tl = cl->next;
			
            cl->next = p->free;//��cl���Сbuf�ڵ����p->free����ngx_http_fastcgi_input_filter�����ظ�ʹ�á�
            p->free = cl;
			
            cl = tl;
        }
    }
}
