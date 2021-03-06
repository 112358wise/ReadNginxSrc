
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{//创建内存池指针，采用大内存直接malloc分配，小内存的方式预分配.不用的先缓存。一次释放。
    ngx_pool_t  *p;
/*NGX_POOL_ALIGNMENT为16*/
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);//k 申请内存
    if (p == NULL) {
        return NULL;
    }
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);//前面保留一个头部
    p->d.end = (u_char *) p + size;//k 前面实际没有那么多数据部分的，有个头部sizeof(ngx_pool_t)
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}

/*
先调用要cleanup内存的handler，
然后释放大块内存。
然后释放小块内存链表
*/
void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

//对cleanup列表遍历调用handler函数
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

    for (l = pool->large; l; l = l->next) {
//对于大块内存，直接free
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
        if (l->alloc) {
            ngx_free(l->alloc);//释放了也不置NULL，什么情况?
        }
    }
#if (NGX_DEBUG)
    /*
     * we could allocate the pool->log from this pool
     * so we can not use this log while the free()ing the pool
     */
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p, unused: %uz", p, p->d.end - p->d.last);
        if (n == NULL) {
            break;
        }
    }
#endif
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);
        if (n == NULL) {
            break;
        }
    }
}


//功能是释放大块内存，小内存只置空
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;
//先把大的全部释放。large本身这个链表怎么不释放?
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
//直接置空?链表本身不用释放吗?o ,god like ，nginx小块内存都不用释放
    pool->large = NULL;
//小块内存不释放，只是重新指向开头，但又不置0
    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    }
}


void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {
		//从当前poll开始，前面的呢?不管
        p = pool->current;
        do {
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT);
            if ((size_t) (p->d.end - m) >= size) {
                p->d.last = m + size;//如果还剩下那么多，那就移动last，然后返回指针就行。
                return m;
            }
//不行再看下一个poll把，但通过data.next指向下一个，怪异
            p = p->d.next;
        } while (p);
//当前池以后的所以池都不能满足这次请求，那我得再申请一整块了。
        return ngx_palloc_block(pool, size);
    }
//如果大于max，则直接申请大块内存
    return ngx_palloc_large(pool, size);
}


//跟ngx_palloc的区别是ngx_pnalloc不对齐
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {//如果是大块内存分配，则直接分配

        p = pool->current;//得到当前的一块

        do {
            m = p->d.last;

            if ((size_t) (p->d.end - m) >= size) {//如果last当前最后的指针到结尾end,够了，就移动一下，返回。
                p->d.last = m + size;//这样的内存申请不用释放
                return m;
            }

            p = p->d.next;

        } while (p);

        return ngx_palloc_block(pool, size);//重新来一块
    }

    return ngx_palloc_large(pool, size);
}


static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new, *current;

    psize = (size_t) (pool->d.end - (u_char *) pool);//一整块有多少去了� 计算一下之前的就知道了
//注意，上面没有申请整个ngx_pool_t结构，而只是这个结构的data部分，不会包括不用的cleanup,large
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);//申请一块跟pool一样大小的
    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;
//good 我申请了一块新的
    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;
//马上分配size出去吧
    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;//移动指针

    current = pool->current;//好的，这是旧的当前指针
//这是啥意思。如果有下一个，那就继续走向下一个，直到尾部
//每次从当前池走到倒数第二个池，全都增加failed,表示刚才我在你那问了一下，你却满足不了我!
//如果你满足不了我超过4次，得了，你和前面的都没机会了，我不在需要你了.
//好绝，如果连续申请几块悲剧大小的，那不悲剧了
//对，但这样能有效避免如果这个地方有很多很小的，几乎不能满足需求，就能巧妙的过滤它，避免总是失败的尝试几次
    for (p = current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {//如果这块失败过4次,current就指向它，并增加所有的错误次数
            current = p->d.next;
        }
    }
//接到最后
    p->d.next = new;

//除非第一次，否则current不会为空。上述for可看出
    pool->current = current ? current : new;

    return m;
}


static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;

    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {//这里在什么时候有用呢?
            large->alloc = p;
            return p;
        }
//在当前large链表中找了3次都没有找到空的位置，干脆申请一个节点算了
        if (n++ > 3) {
            break;
        }
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }
//放到前面
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


//对齐申请一块大的内存
void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


//释放大内存
ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;
//参数p指的是实际内存的地址，没办法，扫一遍。因为我们需要alloc=NULL
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


//靠cleanup靠靠縮ize靠縫縞leanup靠靠
ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;
//靠靠靠�
    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }

    if (size) {//靠靠靠靠
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

	//靠縞leanup靠
    c->handler = NULL;
    c->next = p->cleanup;
    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


//靠p縞leanup靠靠靠靠handler縩gx_pool_cleanup_file,靠靠靠靠
void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}


void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


void
ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
