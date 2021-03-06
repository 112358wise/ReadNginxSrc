
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
{//在有buffering的时候，使用event_pipe进行数据的转发，调用ngx_event_pipe_write_to*函数读取数据，或者发送数据给客户端。
//ngx_event_pipe将upstream响应发送回客户端。do_write代表是否要往客户端发送，写数据。
//如果设置了，那么会先发给客户端，再读upstream数据，当然，如果读取了数据，也会调用这里的。
    u_int         flags;
    ngx_int_t     rc;
    ngx_event_t  *rev, *wev;

//这个for循环是不断的用ngx_event_pipe_read_upstream读取客户端数据，然后调用ngx_event_pipe_write_to_downstream
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
		//从upstream读取数据到chain的链表里面，然后整块整块的调用input_filter进行协议的解析，并将HTTP结果存放在p->in，p->last_in的链表里面。
        if (ngx_event_pipe_read_upstream(p) == NGX_ABORT) {
            return NGX_ABORT;
        }
		//upstream_blocked是在ngx_event_pipe_read_upstream里面设置的变量,代表是否有数据已经从upstream读取了。
        if (!p->read && !p->upstream_blocked) {
            break;
        }
        do_write = 1;//还要写。因为我这次读到了一些数据
    }
	
//下面是处理是否需要设置定时器，或者删除读写事件的epoll。
    if (p->upstream->fd != -1) {//如果后端php等的连接fd是有效的，则注册读写事件。
        rev = p->upstream->read;//得到这个连接的读写事件结构，如果其发生了错误，那么将其读写事件注册删除掉，否则保存原样。
        flags = (rev->eof || rev->error) ? NGX_CLOSE_EVENT : 0;
        if (ngx_handle_read_event(rev, flags) != NGX_OK) {
            return NGX_ABORT;//看看是否需要将这个连接删除读写事件注册。
        }
        if (rev->active && !rev->ready) {//没有读写数据了，那就设置一个读超时定时器
            ngx_add_timer(rev, p->read_timeout);
        } else if (rev->timer_set) {
            ngx_del_timer(rev);
        }
    }
    if (p->downstream->fd != -1 && p->downstream->data == p->output_ctx) {
        wev = p->downstream->write;//对客户端的连接，注册可写事件。关心可写
        if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
            return NGX_ABORT;
        }
        if (!wev->delayed) {
            if (wev->active && !wev->ready) {//同样，注册一下超时。
                ngx_add_timer(wev, p->send_timeout);
            } else if (wev->timer_set) {
                ngx_del_timer(wev);
            }
        }
    }
    return NGX_OK;
}
/*
1.从preread_bufs，free_raw_bufs或者ngx_create_temp_buf寻找一块空闲的或部分空闲的内存；
2.调用p->upstream->recv_chain==ngx_readv_chain，用writev的方式读取FCGI的数据,填充chain。
3.对于整块buf都满了的chain节点调用input_filter(ngx_http_fastcgi_input_filter)进行upstream协议解析，比如FCGI协议，解析后的结果放入p->in里面；
4.对于没有填充满的buffer节点，放入free_raw_bufs以待下次进入时从后面进行追加。
5.当然了，如果对端发送完数据FIN了，那就直接调用input_filter处理free_raw_bufs这块数据。
*/
static ngx_int_t ngx_event_pipe_read_upstream(ngx_event_pipe_t *p)
{//ngx_event_pipe调用这里读取后端的数据。
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
            break;//状态判断。
        }
		//如果没有预读数据，并且跟upstream的连接还没有read，那就可以退出了，因为没数据可读。
        if (p->preread_bufs == NULL && !p->upstream->read->ready) {
            break;
        }
		//下面这个大的if-else就干一件事情: 寻找一块空闲的内存缓冲区，用来待会存放读取进来的upstream的数据。
		//如果preread_bufs不为空，就先用之，否则看看free_raw_bufs有没有，或者申请一块
        if (p->preread_bufs) {//如果预读数据有的话，比如第一次进来，连接尚未可读，但是之前读到了一部分body。那就先处理完这个body再进行读取。
            /* use the pre-read bufs if they exist */
            chain = p->preread_bufs;//那就将这个块的数据链接起来,待会用来存放读入的数据。并清空preread_bufs,
            p->preread_bufs = NULL;
            n = p->preread_size;
            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,  "pipe preread: %z", n);
            if (n) {
                p->read = 1;//读了数据。
            }
        } else {//否则，preread_bufs为空，没有了。
#if (NGX_HAVE_KQUEUE)//中间删除了。不考虑。
#endif
            if (p->free_raw_bufs) {
                /* use the free bufs if they exist */
                chain = p->free_raw_bufs;
                if (p->single_buf) {//如果设置了NGX_USE_AIO_EVENT标志， the posted aio operation may currupt a shadow buffer
                    p->free_raw_bufs = p->free_raw_bufs->next;
                    chain->next = NULL;
                } else {//如果不是AIO，那么可以用多块内存一次用readv读取的。
                    p->free_raw_bufs = NULL;
                }
            } else if (p->allocated < p->bufs.num) {
            //如果没有超过fastcgi_buffers等指令的限制，那么申请一块内存吧。因为现在没有空闲内存了。
                /* allocate a new buf if it's still allowed */
			//申请一个ngx_buf_t以及size大小的数据。用来存储从FCGI读取的数据。
                b = ngx_create_temp_buf(p->pool, p->bufs.size);
                if (b == NULL) {
                    return NGX_ABORT;
                }
                p->allocated++;
                chain = ngx_alloc_chain_link(p->pool);//申请一个链表结构，指向刚申请的那坨buf,这个buf 比较大的。几十K以上。
                if (chain == NULL) {
                    return NGX_ABORT;
                }
                chain->buf = b;
                chain->next = NULL;
            } else if (!p->cacheable && p->downstream->data == p->output_ctx && p->downstream->write->ready && !p->downstream->write->delayed) {
			//到这里，那说明没法申请内存了，但是配置里面没要求必须先保留在cache里，那我们可以吧当前的数据发送给客户端了。跳出循环。
                /*
                 * if the bufs are not needed to be saved in a cache and
                 * a downstream is ready then write the bufs to a downstream
                 */
                p->upstream_blocked = 1;//标记已经读取了数据，可以write了。
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe downstream ready");
                break;
            } else if (p->cacheable || p->temp_file->offset < p->max_temp_file_size)
            {//必须缓存，而且当前的缓存文件的位移，就是大小小于可允许的大小，那good，可以写入文件了。
                /*
                 * if it is allowed, then save some bufs from r->in
                 * to a temporary file, and add them to a r->out chain
                 */
//下面将r->in的数据写到临时文件
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
            } else {//没办法了。
                /* there are no bufs to read in */
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0,  "no pipe bufs to read in");
                break;
            }
			//到这里，肯定是找到空闲的buf了，chain指向之了。先睡觉，电脑没电了。
			//ngx_readv_chain .调用readv不断的读取连接的数据。放入chain的链表里面
			//这里的chain是不是只有一块? 其next成员为空呢，不一定，如果free_raw_bufs不为空，
			//上面的获取空闲buf只要没有使用AIO的话，就可能有多个buffer链表的。
            n = p->upstream->recv_chain(p->upstream, chain);

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe recv chain: %z", n);
            if (p->free_raw_bufs) {//free_raw_bufs不为空，那就将chain指向的这块放到free_raw_bufs头部。
                chain->next = p->free_raw_bufs;
            }
            p->free_raw_bufs = chain;//放入头部
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
                p->upstream_eof = 1;//没有读到数据，肯定upstream发送了FIN包，那就读取完成了。
                break;
            }
        }//从上面for循环刚开始的if (p->preread_bufs) {到这里，都在寻找一个空闲的缓冲区，然后读取数据填充chain。够长的。
//读取了数据，下面要进行FCGI协议解析，保存了。
        p->read_length += n;
        cl = chain;//chain已经是链表的头部了，等于free_raw_bufs所以下面可以置空先。
        p->free_raw_bufs = NULL;

        while (cl && n > 0) {//如果还有链表数据并且长度不为0，也就是这次的还没有处理完。那如果之前保留有一部分数据呢?
        //不会的，如果之前预读了数据，那么上面的大if语句else里面进不去，就是此时的n肯定等于preread_bufs的长度preread_size。
        //如果之前没有预读数据，但free_raw_bufs不为空，那也没关系，free_raw_bufs里面的数据肯定已经在下面几行处理过了。

		//下面的函数将c->buf中用shadow指针连接起来的链表中所有节点的recycled,temporary,shadow成员置空。
            ngx_event_pipe_remove_shadow_links(cl->buf);

            size = cl->buf->end - cl->buf->last;
            if (n >= size) {
                cl->buf->last = cl->buf->end;//把这坨全部用了,readv填充了数据。
                /* STUB */ cl->buf->num = p->num++;//第几块
				//FCGI为ngx_http_fastcgi_input_filter，其他为ngx_event_pipe_copy_input_filter 。用来解析特定格式数据
                if (p->input_filter(p, cl->buf) == NGX_ERROR) {//整块buffer的调用协议解析句柄
                //这里面，如果cl->buf这块数据解析出来了DATA数据，那么cl->buf->shadow成员指向一个链表，
                //通过shadow成员链接起来的链表，每个成员就是零散的fcgi data数据部分。
                    return NGX_ABORT;
                }
                n -= size;
                ln = cl;
                cl = cl->next;//继续处理下一块，并释放这个节点。
                ngx_free_chain(p->pool, ln);

            } else {//如果这个节点的空闲内存数目大于剩下要处理的，就将剩下的存放在这里。
                cl->buf->last += n;//啥意思，不用调用input_filter了吗，不是。是这样的，如果剩下的这块数据还不够塞满当前这个cl的缓存大小，
                n = 0;//那就先存起来，怎么存呢: 别释放cl了，只是移动其大小，然后n=0使循环退出。然后在下面几行的if (cl) {里面可以检测到这种情况
 //于是在下面的if里面会将这个ln处的数据放入free_raw_bufs的头部。不过这里会有多个连接吗? 可能有的。
            }
        }

        if (cl) {
	//将上面没有填满一块内存块的数据链接放到free_raw_bufs的前面。注意上面修改了cl->buf->last，后续的读入数据不会覆盖这些数据的。看ngx_readv_chain
            for (ln = cl; ln->next; ln = ln->next) { /* void */ }
            ln->next = p->free_raw_bufs;//这个不是NULL吗，上面初始化的，不对，因为input_filter可能会将那些没用data部分的fcgi数据包块放入free_raw_bufs直接进行复用。
            p->free_raw_bufs = cl;//这样在下一次循环的时候，也就是上面，会使用free_raw_bufs的。
            //并且，如果循环结束了，会在下面再处理一下这个尾部没有填满整个块的数据。
        }
    }//for循环结束。

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

    if ((p->upstream_eof || p->upstream_error) && p->free_raw_bufs) {//没办法了，都快到头了，或者出现错误了，所以处理一下这块不完整的buffer
        /* STUB */ p->free_raw_bufs->buf->num = p->num++;
		//如果数据读取完毕了，或者后端出现问题了，并且，free_raw_bufs不为空，后面还有一部分数据，
		//当然只可能有一块。那就调用input_filter处理它。FCGI为ngx_http_fastcgi_input_filter 在ngx_http_fastcgi_handler里面设置的

		//这里考虑一种情况: 这是最后一块数据了，没满，里面没有data数据，所以ngx_http_fastcgi_input_filter会调用ngx_event_pipe_add_free_buf函数，
		//将这块内存放入free_raw_bufs的前面，可是君不知，这最后一块不存在数据部分的内存正好等于free_raw_bufs，因为free_raw_bufs还没来得及改变。
		//所以，就把自己给替换掉了。这种情况会发生吗?
        if (p->input_filter(p, p->free_raw_bufs->buf) == NGX_ERROR) {
            return NGX_ABORT;
        }
        p->free_raw_bufs = p->free_raw_bufs->next;
        if (p->free_bufs && p->buf_to_file == NULL) {
            for (cl = p->free_raw_bufs; cl; cl = cl->next) {
                if (cl->buf->shadow == NULL) 
			//这个shadow成员指向由我这块buf产生的小FCGI数据块buf的指针列表。如果为NULL，就说明这块buf没有data，可以释放了。
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
{//ngx_event_pipe调用这里进行数据发送给客户端，数据已经准备在p->out,p->in里面了。
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
        if (p->downstream_error) {//如果客户端连接出错了。drain=排水；流干,
        //清空upstream发过来的，解析过格式后的HTML数据。将其放入free_raw_bufs里面。
            return ngx_event_pipe_drain_chains(p);
        }
        if (p->upstream_eof || p->upstream_error || p->upstream_done) {
//如果upstream的连接已经关闭了，或出问题了，或者发送完毕了，那就可以发送了。
            /* pass the p->out and p->in chains to the output filter */
            for (cl = p->busy; cl; cl = cl->next) {
                cl->buf->recycled = 0;
            }

            if (p->out) {//数据写到磁盘了
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream flush out");
                for (cl = p->out; cl; cl = cl->next) {
                    cl->buf->recycled = 0;//不需要回收重复利用了，因为upstream_done了，不会再给我发送数据了。
                }
				//下面，因为p->out的链表里面一块块都是解析后的HTML数据，所以直接调用ngx_http_output_filter进行HTML数据发送就行了。
                rc = p->output_filter(p->output_ctx, p->out);
                if (rc == NGX_ERROR) {
                    p->downstream_error = 1;
                    return ngx_event_pipe_drain_chains(p);
                }
                p->out = NULL;
            }

            if (p->in) {//跟out同理。简单调用ngx_http_output_filter进入各个filter发送过程中。
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream flush in");
                for (cl = p->in; cl; cl = cl->next) {
                    cl->buf->recycled = 0;//已经是最后的了，不需要回收了
                }
				//注意下面的发送不是真的writev了，得看具体情况比如是否需要recycled,是否是最后一块等。ngx_http_write_filter会判断这个的。
                rc = p->output_filter(p->output_ctx, p->in);//调用ngx_http_output_filter发送，最后一个是ngx_http_write_filter
                if (rc == NGX_ERROR) {
                    p->downstream_error = 1;
                    return ngx_event_pipe_drain_chains(p);
                }
                p->in = NULL;
            }
			//如果要缓存，那就写入到文件里面去。
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

		//否则upstream数据还没有发送完毕。
        if (downstream->data != p->output_ctx || !downstream->write->ready || downstream->write->delayed) {
            break;
        }
        /* bsize is the size of the busy recycled bufs */
        prev = NULL;
        bsize = 0;
//这里遍历需要busy这个正在发送，已经调用过output_filter的buf链表，计算一下那些可以回收重复利用的buf
//计算这些buf的总容量，注意这里不是计算busy中还有多少数据没有真正writev出去，而是他们总共的最大容量
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
		//busy_size为fastcgi_busy_buffers_size 指令设置的大小，指最大待发送的busy状态的内存总大小。
		//如果大于这个大小，nginx会尝试去发送新的数据并回收这些busy状态的buf。
        if (bsize >= (size_t) p->busy_size) {
            flush = 1;//如果busy链表里面的数据很多了，超过fastcgi_busy_buffers_size 指令，那就赶紧去发送，回收吧，不然free_raw_bufs里面没可用缓存了。
            goto flush;
        }

        flush = 0;
        ll = NULL;
        prev_last_shadow = 1;//标记上一个节点是不是正好是一块FCGI buffer的最后一个数据节点。
//遍历p->out,p->in里面的未发送数据，将他们放到out链表后面，注意这里发送的数据不超过busy_size因为配置限制了。
        for ( ;; ) {
//循环，这个循环的终止后，我们就能获得几块HTML数据节点，并且他们跨越了1个以上的FCGI数据块的并以最后一块带有last_shadow结束。
            if (p->out) {//buf到tempfile的数据会放到out里面。
                cl = p->out;
                if (cl->buf->recycled && bsize + cl->buf->last - cl->buf->pos > p->busy_size) {
                    flush = 1;//判断是否超过busy_size
                    break;
                }
                p->out = p->out->next;
                ngx_event_pipe_free_shadow_raw_buf(&p->free_raw_bufs, cl->buf);
            } else if (!p->cacheable && p->in) {
                cl = p->in;
                ngx_log_debug3(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write buf ls:%d %p %z", cl->buf->last_shadow, cl->buf->pos, cl->buf->last - cl->buf->pos);
				//
                if (cl->buf->recycled && cl->buf->last_shadow && bsize + cl->buf->last - cl->buf->pos > p->busy_size)  {
					//1.对于在in里面的数据，如果其需要回收;
					//2.并且又是某一块大FCGI buf的最后一个有效html数据节点；
					//3.而且当前的没法送的大小大于busy_size, 那就需要回收一下了，因为我们有buffer机制
                    if (!prev_last_shadow) {
		//如果前面的一块不是某个大FCGI buffer的最后一个数据块，那就将当前这块放入out的后面，然后退出循环去flash
		//什么意思呢，就是说，如果当前的这块不会导致out链表多加了一个节点，而倒数第二个节点正好是一块FCGI大内存的结尾。
		//其实是i做了个优化,让nginx尽量一块块的发送。
                        p->in = p->in->next;
                        cl->next = NULL;
                        if (out) {
                            *ll = cl;
                        } else {
                            out = cl;
                        }
                    }
                    flush = 1;//超过了大小，标记一下待会是需要真正发送的。不过这个好像没发挥多少作用，因为后面不怎么判断、
                    break;//停止处理后面的内存块，因为这里已经大于busy_size了。
                }
                prev_last_shadow = cl->buf->last_shadow;
                p->in = p->in->next;
            } else {
                break;//后面没有数据了，那没办法了，发吧。不过一般情况肯定有last_shadow为1的。这里很难进来的。
            }
//cl指向当前需要处理的数据，比如cl = p->out或者cl = p->in;
//下面就将这块内存放入out指向的链表的最后，ll指向最后一块的next指针地址。
            if (cl->buf->recycled) {//如果这块buf是需要回收利用的，就统计其大小
                bsize += cl->buf->last - cl->buf->pos;
            }
            cl->next = NULL;
            if (out) {
                *ll = cl;
            } else {
                out = cl;//指向第一块数据
            }
            ll = &cl->next;
        }
//到这里后，out指针指向一个链表，其里面的数据是从p->out,p->in来的要发送的数据。
    flush:
//下面将out指针指向的内存调用output_filter，进入filter过程。
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
        rc = p->output_filter(p->output_ctx, out);//简单调用ngx_http_output_filter进入各个filter发送过程中。
        if (rc == NGX_ERROR) {
            p->downstream_error = 1;
            return ngx_event_pipe_drain_chains(p);
        }
		//将out的数据移动到busy，busy中发送完成的移动到free
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
	//前面说过了，如果这块内存正好是整个大FCGI裸内存的最后一个data节点，则释放这块大FCGI buffer。
	//当last_shadow为1的时候，buf->shadow实际上指向了这块大的FCGI裸buf的。也就是原始buf，其他buf都是个影子，他们指向某块原始的buf.
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
{//删除数据的shadow，以及recycled设置为0，表示不需要循环利用，这里实现了buffering功能
//因为ngx_http_write_filter函数里面判断如果有recycled标志，就会立即将数据发送出去，
//因此这里将这些标志清空，到ngx_http_write_filter那里就会尽量缓存的。
    ngx_buf_t  *b, *next;

    b = buf->shadow;//这个shadow指向的是buf这块裸FCGI数据的第一个数据节点
    if (b == NULL) {
        return;
    }
    while (!b->last_shadow) {//如果不是最后一个数据节点，不断往后遍历，
        next = b->shadow;
        b->temporary = 0;
        b->recycled = 0;//标记为回收的 �
        b->shadow = NULL;//把shadow成员置空。
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
	//下面不断的沿着当前的buf往后走，直到走到了本大FCGI裸数据块的最后一个节点数据块。
    for (s = buf->shadow; !s->last_shadow; s = s->shadow) { /* void */ }

    ll = free;
    for (cl = *free; cl; cl = cl->next) {
        if (cl->buf == s) {//是最后一块的话
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
{//将参数的b代表的数据块挂入free_raw_bufs的开头或者第二个位置。b为上层觉得没用了的数据块。
    ngx_chain_t  *cl;
//这里不会出现b就等于free_raw_bufs->buf的情况吗
    cl = ngx_alloc_chain_link(p->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    b->pos = b->start;//置空这坨数据
    b->last = b->start;
    b->shadow = NULL;

    cl->buf = b;

    if (p->free_raw_bufs == NULL) {
        p->free_raw_bufs = cl;
        cl->next = NULL;
        return NGX_OK;
    }
	//看下面的注释，意思是，如果最前面的free_raw_bufs中没有数据，那就吧当前这块数据放入头部就行。
	//否则如果当前free_raw_bufs有数据，那就得放到其后面了。为什么会有数据呢?比如，读取一些数据后，还剩下一个尾巴存放在free_raw_bufs，然后开始往客户端写数据
	//写完后，自然要把没用的buffer放入到这里面来。这个是在ngx_event_pipe_write_to_downstream里面做的。或者干脆在ngx_event_pipe_drain_chains里面做。
	//因为这个函数在inpupt_filter里面调用是从数据块开始处理，然后到后面的，
	//并且在调用input_filter之前是会将free_raw_bufs置空的。应该是其他地方也有调用。
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
{//遍历p->in/out/busy，将其链表所属的裸FCGI数据块释放，放入到free_raw_bufs中间去。也就是，清空upstream发过来的，解析过格式后的HTML数据。
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
		//找到对应的链表
        while (cl) {/*要知道，这里cl里面，比如p->in里面的这些ngx_buf_t结构所指向的数据内存实际上是在
        ngx_event_pipe_read_upstream里面的input_filter进行协议解析的时候设置为跟从客户端读取数据时的buf公用的，也就是所谓的影子。
		然后，虽然p->in指向的链表里面有很多很多个节点，每个节点代表一块HTML代码，但是他们并不是独占一块内存的，而是可能共享的，
		比如一块大的buffer，里面有3个FCGI的STDOUT数据包，都有data部分，那么将存在3个b的节点链接到p->in的末尾，他们的shadow成员
		分别指向下一个节点，最后一个节点就指向其所属的大内存结构。具体在ngx_http_fastcgi_input_filter实现。
        */
            if (cl->buf->last_shadow) {//碰到了某个大FCGI数据块的最后一个节点，释放只，然后进入下一个大块里面的某个小html 数据块。
                if (ngx_event_pipe_add_free_buf(p, cl->buf->shadow) != NGX_OK) {
                    return NGX_ABORT;
                }
                cl->buf->last_shadow = 0;
            }

            cl->buf->shadow = NULL;
            tl = cl->next;
			
            cl->next = p->free;//把cl这个小buf节点放入p->free，供ngx_http_fastcgi_input_filter进行重复使用。
            p->free = cl;
			
            cl = tl;
        }
    }
}
