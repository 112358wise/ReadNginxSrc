
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if 0
#define NGX_SENDFILE_LIMIT  4096
#endif

/*
 * When DIRECTIO is enabled FreeBSD, Solaris, and MacOSX read directly
 * to an application memory from a device if parameters are aligned
 * to device sector boundary (512 bytes).  They fallback to usual read
 * operation if the parameters are not aligned.
 * Linux allows DIRECTIO only if the parameters are aligned to a filesystem
 * sector boundary, otherwise it returns EINVAL.  The sector size is
 * usually 512 bytes, however, on XFS it may be 4096 bytes.
 */

#define NGX_NONE            1


static ngx_inline ngx_int_t
    ngx_output_chain_as_is(ngx_output_chain_ctx_t *ctx, ngx_buf_t *buf);
static ngx_int_t ngx_output_chain_add_copy(ngx_pool_t *pool,
    ngx_chain_t **chain, ngx_chain_t *in);
static ngx_int_t ngx_output_chain_align_file_buf(ngx_output_chain_ctx_t *ctx,
    off_t bsize);
static ngx_int_t ngx_output_chain_get_buf(ngx_output_chain_ctx_t *ctx,
    off_t bsize);
static ngx_int_t ngx_output_chain_copy_buf(ngx_output_chain_ctx_t *ctx);


ngx_int_t
ngx_output_chain(ngx_output_chain_ctx_t *ctx, ngx_chain_t *in)
{//ctx为&u->output， in为u->request_bufs
//这里nginx filter的主要逻辑都在这个函数里面,将in参数链表的缓冲块拷贝到
//ctx->in,然后将ctx->in的数据拷贝到out,然后调用output_filter发送出去。
    off_t         bsize;
    ngx_int_t     rc, last;
    ngx_chain_t  *cl, *out, **last_out;

    if (ctx->in == NULL && ctx->busy == NULL) {
		//看下面的注释，in是待发送的数据，busy是已经调用ngx_chain_writer但还没有发送完毕。
        /* the short path for the case when the ctx->in and ctx->busy chains
          * are empty, the incoming chain is empty too or has the single buf that does not require the copy
         */
        if (in == NULL) {//如果要发送的数据为空，也就是啥也不用发送。那就直接调用output_filter的了。
        //调用的是ngx_chain_writer，在ngx_http_upstream_init_request初始化的时候设置的输出数据。
            return ctx->output_filter(ctx->filter_ctx, in);
        }
        if (in->next == NULL //如果输出缓冲不为空，但是只有一块数据，那也可以直接发送不用拷贝了
#if (NGX_SENDFILE_LIMIT)
            && !(in->buf->in_file && in->buf->file_last > NGX_SENDFILE_LIMIT)
#endif
            && ngx_output_chain_as_is(ctx, in->buf))//这个函数主要用来判断是否需要复制buf。返回1,表示不需要拷贝，否则为需要拷贝 
        {
            return ctx->output_filter(ctx->filter_ctx, in);
        }
    }
    /* add the incoming buf to the chain ctx->in */
    if (in) {//拷贝一份数据到ctx->in里面，需要老老实实的进行数据拷贝了。将in参数里面的数据拷贝到ctx->in里面。换了个in
        if (ngx_output_chain_add_copy(ctx->pool, &ctx->in, in) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }
	//到现在了，in参数的缓冲链表已经放在了ctx->in里面了。下面准备发送吧。
    out = NULL;
    last_out = &out;
    last = NGX_NONE;
    for ( ;; ) {
#if (NGX_HAVE_FILE_AIO)
        if (ctx->aio) {
            return NGX_AGAIN;
        }
#endif
        while (ctx->in) {//遍历所有待发送的数据。将他们一个个拷贝到out指向的链表中，为什么要拷贝呢，不知道
            /* cycle while there are the ctx->in bufs
             * and there are the free output bufs to copy in
             */
            bsize = ngx_buf_size(ctx->in->buf);
			//这块内存大小为0，可能有问题。
            if (bsize == 0 && !ngx_buf_special(ctx->in->buf)) {
                ngx_log_error(NGX_LOG_ALERT, ctx->pool->log, 0, "zero size buf in output t:%d r:%d f:%d %p %p-%p %p %O-%O",
                              ctx->in->buf->temporary, ctx->in->buf->recycled, ctx->in->buf->in_file,  ctx->in->buf->start, ctx->in->buf->pos,
                              ctx->in->buf->last,  ctx->in->buf->file,  ctx->in->buf->file_pos,  ctx->in->buf->file_last);
                ngx_debug_point();
                ctx->in = ctx->in->next;
                continue;
            }
			//这块数据不能拷贝的话，就只能改变一下指向进行共享了，不能拷贝实际数据了。
            if (ngx_output_chain_as_is(ctx, ctx->in->buf)) { /* move the chain link to the output chain */
                cl = ctx->in;//buf不需要拷贝，改变一下指向就行了。
                ctx->in = cl->next;
                *last_out = cl;//初始化时，last_out = &out;这里将cl这个头部的节点用out指向。out指向这种处理过的节点头部。
                last_out = &cl->next;//last_out指向下一个节点。下面继续。
                cl->next = NULL;//截断这个几点与后面还未处理的节点，然后继续。下次循环会继续往后添加的。
                continue;
            }
			//后面的是需要实际拷贝内存的。
            if (ctx->buf == NULL) {
                rc = ngx_output_chain_align_file_buf(ctx, bsize);
                if (rc == NGX_ERROR) {
                    return NGX_ERROR;
                }
                if (rc != NGX_OK) {
                    if (ctx->free) { /* get the free buf */
                        cl = ctx->free;
                        ctx->buf = cl->buf;
                        ctx->free = cl->next;
                        ngx_free_chain(ctx->pool, cl);
                    } else if (out || ctx->allocated == ctx->bufs.num) {
                        break;
                    } else if (ngx_output_chain_get_buf(ctx, bsize) != NGX_OK) {
                        return NGX_ERROR;
                    }
                }
            }
            rc = ngx_output_chain_copy_buf(ctx);//将ctx->in->buf的缓冲拷贝到ctx->buf上面去。会创建一个新的节点。
            if (rc == NGX_ERROR) {
                return rc;
            }
            if (rc == NGX_AGAIN) {
                if (out) {
                    break;
                }
                return rc;
            }
            /* delete the completed buf from the ctx->in chain */
            if (ngx_buf_size(ctx->in->buf) == 0) {
                ctx->in = ctx->in->next;//这个节点大小为0，移动到下一个节点。
            }

            cl = ngx_alloc_chain_link(ctx->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }
            cl->buf = ctx->buf;
            cl->next = NULL;
            *last_out = cl;//将这个节点拷贝到last_out所指向的节点，也就是out指针指向的头部的链表后面。
            last_out = &cl->next;
            ctx->buf = NULL;
        }//while (ctx->in) 结束，遍历ctx->in完毕，也就是处理完了所有待发送节点，将他们

        if (out == NULL && last != NGX_NONE) {
            if (ctx->in) {
                return NGX_AGAIN;
            }
            return last;
        }
		//out为刚刚处理，拷贝过的缓冲区链表头部，下面进行发送了吧应该。调用的是ngx_chain_writer
        last = ctx->output_filter(ctx->filter_ctx, out);
        if (last == NGX_ERROR || last == NGX_DONE) {
            return last;
        }
        ngx_chain_update_chains(&ctx->free, &ctx->busy, &out, ctx->tag);
        last_out = &out;//
    }//for ( ;; ) 这是个死循环，也就是不断的处理out所指向的数据。
}


static ngx_inline ngx_int_t
ngx_output_chain_as_is(ngx_output_chain_ctx_t *ctx, ngx_buf_t *buf)
{//看看这个节点是否可以拷贝。检测是否content是否在文件中。判断是否需要复制buf.
//返回1表示上层不需要拷贝buf,否则需要重新alloc一个节点，拷贝实际内存到另外一个节点。
    ngx_uint_t  sendfile;

    if (ngx_buf_special(buf)) {//不再文件中。
        return 1;
    }
    if (buf->in_file && buf->file->directio) {
        return 0;//如果buf在文件中，使用了directio，需要拷贝buf
    }
    sendfile = ctx->sendfile;
#if (NGX_SENDFILE_LIMIT)
    if (buf->in_file && buf->file_pos >= NGX_SENDFILE_LIMIT) {
        sendfile = 0;
    }
#endif
    if (!sendfile) {
        if (!ngx_buf_in_memory(buf)) {
            return 0;
        }
        buf->in_file = 0;
    }
    if (ctx->need_in_memory && !ngx_buf_in_memory(buf)) {
        return 0;
    }
    if (ctx->need_in_temp && (buf->memory || buf->mmap)) {
        return 0;
    }
    return 1;
}


static ngx_int_t ngx_output_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{//ngx_output_chain调用这里，将u->request_bufs也就是参数 in的数据拷贝到chain里面。
//参数为:(ctx->pool, &ctx->in, in)。in代表要发送的，也就是输入的缓冲区链表。
    ngx_chain_t  *cl, **ll;
#if (NGX_SENDFILE_LIMIT)
    ngx_buf_t    *b, *buf;
#endif

    ll = chain;//ll指向链表头部。chain里面可能有数据，所以需要追加到末尾
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;//一直找到这个缓冲链表的最后面。
    }
    while (in) {//遍历所有的
        cl = ngx_alloc_chain_link(pool);//在池子里分配一个ngx_chain_t
        if (cl == NULL) {
            return NGX_ERROR;
        }
#if (NGX_SENDFILE_LIMIT)
        buf = in->buf;//如果缓冲buffer在文件中，并且文件没有超过限制，那就考吧它，但是，如果这个文件超过了limit，那肿么办，拆分成2快buffer。
        if (buf->in_file && buf->file_pos < NGX_SENDFILE_LIMIT && buf->file_last > NGX_SENDFILE_LIMIT)  {
            /* split a file buf on two bufs by the sendfile limit */
            b = ngx_calloc_buf(pool);
            if (b == NULL) {
                return NGX_ERROR;
            }
            ngx_memcpy(b, buf, sizeof(ngx_buf_t));
            if (ngx_buf_in_memory(buf)) {
                buf->pos += (ssize_t) (NGX_SENDFILE_LIMIT - buf->file_pos);
                b->last = buf->pos;
            }
            buf->file_pos = NGX_SENDFILE_LIMIT;
            b->file_last = NGX_SENDFILE_LIMIT;
            cl->buf = b;
        } else {
            cl->buf = buf;
            in = in->next;
        }
#else
        cl->buf = in->buf;//记录buf缓冲区块。
        in = in->next;//走到输入链表的下一个，准备拷贝下一个。
#endif
        cl->next = NULL;
        *ll = cl;//我是下一个
        ll = &cl->next;//记录梦想中的下一个节点的位置。待会可以直接把这个地址的值改为cl.
    }
    return NGX_OK;
}


static ngx_int_t
ngx_output_chain_align_file_buf(ngx_output_chain_ctx_t *ctx, off_t bsize)
{
    size_t      size;
    ngx_buf_t  *in;

    in = ctx->in->buf;

    if (in->file == NULL || !in->file->directio) {
        return NGX_DECLINED;
    }

    ctx->directio = 1;

    size = (size_t) (in->file_pos - (in->file_pos & ~(ctx->alignment - 1)));

    if (size == 0) {

        if (bsize >= (off_t) ctx->bufs.size) {
            return NGX_DECLINED;
        }

        size = (size_t) bsize;

    } else {
        size = (size_t) ctx->alignment - size;

        if ((off_t) size > bsize) {
            size = (size_t) bsize;
        }
    }

    ctx->buf = ngx_create_temp_buf(ctx->pool, size);
    if (ctx->buf == NULL) {
        return NGX_ERROR;
    }

    /*
     * we do not set ctx->buf->tag, because we do not want
     * to reuse the buf via ctx->free list
     */

#if (NGX_HAVE_ALIGNED_DIRECTIO)
    ctx->unaligned = 1;
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_output_chain_get_buf(ngx_output_chain_ctx_t *ctx, off_t bsize)
{
    size_t       size;
    ngx_buf_t   *b, *in;
    ngx_uint_t   recycled;

    in = ctx->in->buf;
    size = ctx->bufs.size;
    recycled = 1;

    if (in->last_in_chain) {

        if (bsize < (off_t) size) {

            /*
             * allocate a small temp buf for a small last buf
             * or its small last part
             */

            size = (size_t) bsize;
            recycled = 0;

        } else if (!ctx->directio
                   && ctx->bufs.num == 1
                   && (bsize < (off_t) (size + size / 4)))
        {
            /*
             * allocate a temp buf that equals to a last buf,
             * if there is no directio, the last buf size is lesser
             * than 1.25 of bufs.size and the temp buf is single
             */

            size = (size_t) bsize;
            recycled = 0;
        }
    }

    b = ngx_calloc_buf(ctx->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    if (ctx->directio) {

        /*
         * allocate block aligned to a disk sector size to enable
         * userland buffer direct usage conjunctly with directio
         */

        b->start = ngx_pmemalign(ctx->pool, size, (size_t) ctx->alignment);
        if (b->start == NULL) {
            return NGX_ERROR;
        }

    } else {
        b->start = ngx_palloc(ctx->pool, size);
        if (b->start == NULL) {
            return NGX_ERROR;
        }
    }

    b->pos = b->start;
    b->last = b->start;
    b->end = b->last + size;
    b->temporary = 1;
    b->tag = ctx->tag;
    b->recycled = recycled;

    ctx->buf = b;
    ctx->allocated++;

    return NGX_OK;
}


static ngx_int_t
ngx_output_chain_copy_buf(ngx_output_chain_ctx_t *ctx)
{//将ctx->in->buf的缓冲拷贝到ctx->buf上面去。
    off_t        size;
    ssize_t      n;
    ngx_buf_t   *src, *dst;
    ngx_uint_t   sendfile;

    src = ctx->in->buf;
    dst = ctx->buf;
    size = ngx_buf_size(src);
    size = ngx_min(size, dst->end - dst->pos);
    sendfile = ctx->sendfile & !ctx->directio;

#if (NGX_SENDFILE_LIMIT)
    if (src->in_file && src->file_pos >= NGX_SENDFILE_LIMIT) {
        sendfile = 0;
    }
#endif

    if (ngx_buf_in_memory(src)) {//如果数据在内存里，直接进行拷贝就行。相当于b->temporary || b->memory || b->mmap
        ngx_memcpy(dst->pos, src->pos, (size_t) size);
        src->pos += (size_t) size;
        dst->last += (size_t) size;

        if (src->in_file) {
            if (sendfile) {
                dst->in_file = 1;
                dst->file = src->file;
                dst->file_pos = src->file_pos;
                dst->file_last = src->file_pos + size;

            } else {
                dst->in_file = 0;
            }

            src->file_pos += size;

        } else {
            dst->in_file = 0;
        }

        if (src->pos == src->last) {
            dst->flush = src->flush;
            dst->last_buf = src->last_buf;
            dst->last_in_chain = src->last_in_chain;
        }

    } else {//否则，文件不再内存里面，需要从磁盘读取。

#if (NGX_HAVE_ALIGNED_DIRECTIO)
        if (ctx->unaligned) {
            if (ngx_directio_off(src->file->fd) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_ALERT, ctx->pool->log, ngx_errno, ngx_directio_off_n " \"%s\" failed", src->file->name.data);
            }
        }
#endif
#if (NGX_HAVE_FILE_AIO)
        if (ctx->aio_handler) {
            n = ngx_file_aio_read(src->file, dst->pos, (size_t) size,  src->file_pos, ctx->pool);
            if (n == NGX_AGAIN) {
                ctx->aio_handler(ctx, src->file);
                return NGX_AGAIN;
            }

        } else {
            n = ngx_read_file(src->file, dst->pos, (size_t) size, src->file_pos);
        }
#else
        n = ngx_read_file(src->file, dst->pos, (size_t) size, src->file_pos);
#endif

#if (NGX_HAVE_ALIGNED_DIRECTIO)

        if (ctx->unaligned) {
            ngx_err_t  err;
            err = ngx_errno;
            if (ngx_directio_on(src->file->fd) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_ALERT, ctx->pool->log, ngx_errno, ngx_directio_on_n " \"%s\" failed",  src->file->name.data);
            }
            ngx_set_errno(err);
            ctx->unaligned = 0;
        }

#endif
        if (n == NGX_ERROR) {
            return (ngx_int_t) n;
        }
        if (n != size) {
            ngx_log_error(NGX_LOG_ALERT, ctx->pool->log, 0, ngx_read_file_n " read only %z of %O from \"%s\"", n, size, src->file->name.data);
            return NGX_ERROR;
        }

        dst->last += n;
        if (sendfile) {
            dst->in_file = 1;
            dst->file = src->file;
            dst->file_pos = src->file_pos;
            dst->file_last = src->file_pos + n;

        } else {
            dst->in_file = 0;
        }
        src->file_pos += n;
        if (src->file_pos == src->file_last) {
            dst->flush = src->flush;
            dst->last_buf = src->last_buf;
            dst->last_in_chain = src->last_in_chain;
        }
    }
    return NGX_OK;
}


ngx_int_t ngx_chain_writer(void *data, ngx_chain_t *in)
{//ngx_output_chain调用这里，将数据发送出去。数据已经拷贝到in参数里面了。嗲用方式;(ctx->filter_ctx, out);，out为要发送的buf链表头部。
    ngx_chain_writer_ctx_t *ctx = data;//ctx永远指向链表头部。其next指针指向下一个节点。但是last指针却指向
    off_t              size;
    ngx_chain_t       *cl;
    ngx_connection_t  *c;
    c = ctx->connection;
	/*下面的循环，将in里面的每一个链接节点，添加到ctx->filter_ctx所指的链表中。并记录这些in的链表的大小。*/
    for (size = 0; in; in = in->next) {//遍历整个输入缓冲链表。将输入缓冲挂接到ctx里面。
        if (ngx_buf_size(in->buf) == 0 && !ngx_buf_special(in->buf)) {
            ngx_debug_point();
        }
        size += ngx_buf_size(in->buf);//计算这个节点的数据大小。进行累加。这个大小目前其实没什么用，只是为了判断是否还有数据没有发送完毕。
        ngx_log_debug2(NGX_LOG_DEBUG_CORE, c->log, 0, "chain writer buf fl:%d s:%uO", in->buf->flush, ngx_buf_size(in->buf));
        cl = ngx_alloc_chain_link(ctx->pool);//分配一个链接节点，待会挂到ctx里面
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = in->buf;//指向输入的缓冲
        cl->next = NULL;
        *ctx->last = cl;//挂到ctx的last处，也就是挂载到链表的最后面。
        //初始化的时候是u->writer.last = &u->writer.out;，也就是初始时指向自己的头部地址。
        ctx->last = &cl->next;//向后移动last指针，指向新的最后一个节点的next变量地址。
    }
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, c->log, 0, "chain writer in: %p", ctx->out);

    for (cl = ctx->out; cl; cl = cl->next) {//遍历刚刚准备的链表，并统计其大小，这是啥意思?ctx->out为链表头，所以这里遍历的是所有的。
        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_debug_point();
        }
        size += ngx_buf_size(cl->buf);
    }
    if (size == 0 && !c->buffered) {//啥数据都么有，不用发了都
        return NGX_OK;
    }
	//调用writev将ctx->out的数据全部发送出去。如果没法送完，则返回没发送完毕的部分。记录到out里面
	//在ngx_event_connect_peer连接上游服务器的时候设置的发送链接函数ngx_send_chain=ngx_writev_chain。
    ctx->out = c->send_chain(c, ctx->out, ctx->limit);
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, c->log, 0,  "chain writer out: %p", ctx->out);
    if (ctx->out == NGX_CHAIN_ERROR) {
        return NGX_ERROR;
    }
    if (ctx->out == NULL) {//如果没有输出的，则怎么办呢
        ctx->last = &ctx->out;//标记结尾。没东西了。
        if (!c->buffered) {
            return NGX_OK;
        }
    }

    return NGX_AGAIN;
}
