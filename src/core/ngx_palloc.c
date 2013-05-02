
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{//�����ڴ��ָ�룬���ô��ڴ�ֱ��malloc���䣬С�ڴ�ķ�ʽԤ����.���õ��Ȼ��档һ���ͷš�
    ngx_pool_t  *p;
/*NGX_POOL_ALIGNMENTΪ16*/
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);//k �����ڴ�
    if (p == NULL) {
        return NULL;
    }
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);//ǰ�汣��һ��ͷ��
    p->d.end = (u_char *) p + size;//k ǰ��ʵ��û����ô�����ݲ��ֵģ��и�ͷ��sizeof(ngx_pool_t)
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
�ȵ���Ҫcleanup�ڴ��handler��
Ȼ���ͷŴ���ڴ档
Ȼ���ͷ�С���ڴ�����
*/
void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

//��cleanup�б��������handler����
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

    for (l = pool->large; l; l = l->next) {
//���ڴ���ڴ棬ֱ��free
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
        if (l->alloc) {
            ngx_free(l->alloc);//�ͷ���Ҳ����NULL��ʲô���?
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


//�������ͷŴ���ڴ棬С�ڴ�ֻ�ÿ�
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;
//�ȰѴ��ȫ���ͷš�large�������������ô���ͷ�?
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
//ֱ���ÿ�?���������ͷ���?o ,god like ��nginxС���ڴ涼�����ͷ�
    pool->large = NULL;
//С���ڴ治�ͷţ�ֻ������ָ��ͷ�����ֲ���0
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
		//�ӵ�ǰpoll��ʼ��ǰ�����?����
        p = pool->current;
        do {
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT);
            if ((size_t) (p->d.end - m) >= size) {
                p->d.last = m + size;//�����ʣ����ô�࣬�Ǿ��ƶ�last��Ȼ�󷵻�ָ����С�
                return m;
            }
//�����ٿ���һ��poll�ѣ���ͨ��data.nextָ����һ��������
            p = p->d.next;
        } while (p);
//��ǰ���Ժ�����Գض�������������������ҵ�������һ�����ˡ�
        return ngx_palloc_block(pool, size);
    }
//�������max����ֱ���������ڴ�
    return ngx_palloc_large(pool, size);
}


//��ngx_palloc��������ngx_pnalloc������
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {//����Ǵ���ڴ���䣬��ֱ�ӷ���

        p = pool->current;//�õ���ǰ��һ��

        do {
            m = p->d.last;

            if ((size_t) (p->d.end - m) >= size) {//���last��ǰ����ָ�뵽��βend,���ˣ����ƶ�һ�£����ء�
                p->d.last = m + size;//�������ڴ����벻���ͷ�
                return m;
            }

            p = p->d.next;

        } while (p);

        return ngx_palloc_block(pool, size);//������һ��
    }

    return ngx_palloc_large(pool, size);
}


static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new, *current;

    psize = (size_t) (pool->d.end - (u_char *) pool);//һ�����ж���ȥ�˿ ����һ��֮ǰ�ľ�֪����
//ע�⣬����û����������ngx_pool_t�ṹ����ֻ������ṹ��data���֣�����������õ�cleanup,large
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);//����һ���poolһ����С��
    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;
//good ��������һ���µ�
    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;
//���Ϸ���size��ȥ��
    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;//�ƶ�ָ��

    current = pool->current;//�õģ����Ǿɵĵ�ǰָ��
//����ɶ��˼���������һ�����Ǿͼ���������һ����ֱ��β��
//ÿ�δӵ�ǰ���ߵ������ڶ����أ�ȫ������failed,��ʾ�ղ�������������һ�£���ȴ���㲻����!
//��������㲻���ҳ���4�Σ����ˣ����ǰ��Ķ�û�����ˣ��Ҳ�����Ҫ����.
//�þ�������������뼸�鱯���С�ģ��ǲ�������
//�ԣ�����������Ч�����������ط��кܶ��С�ģ����������������󣬾�������Ĺ���������������ʧ�ܵĳ��Լ���
    for (p = current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {//������ʧ�ܹ�4��,current��ָ���������������еĴ������
            current = p->d.next;
        }
    }
//�ӵ����
    p->d.next = new;

//���ǵ�һ�Σ�����current����Ϊ�ա�����for�ɿ���
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
        if (large->alloc == NULL) {//������ʲôʱ��������?
            large->alloc = p;
            return p;
        }
//�ڵ�ǰlarge����������3�ζ�û���ҵ��յ�λ�ã��ɴ�����һ���ڵ�����
        if (n++ > 3) {
            break;
        }
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }
//�ŵ�ǰ��
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


//��������һ�����ڴ�
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


//�ͷŴ��ڴ�
ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;
//����pָ����ʵ���ڴ�ĵ�ַ��û�취��ɨһ�顣��Ϊ������Ҫalloc=NULL
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


//��cleanup�����size���p�cleanup����
ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;
//�������
    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }

    if (size) {//��������
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

	//���cleanup��
    c->handler = NULL;
    c->next = p->cleanup;
    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


//��p�cleanup��������handler�ngx_pool_cleanup_file,��������
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
