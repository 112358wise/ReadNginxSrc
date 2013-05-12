
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    a = ngx_palloc(p, sizeof(ngx_array_t));
    if (a == NULL) {
        return NULL;
    }

    a->elts = ngx_palloc(p, n * size);
    if (a->elts == NULL) {
        return NULL;
    }

    a->nelts = 0;
    a->size = size;
    a->nalloc = n;
    a->pool = p;

    return a;
}


void
ngx_array_destroy(ngx_array_t *a)
{
    ngx_pool_t  *p;

    p = a->pool;
//if it happens that destory the last buf unit , we return it for reuse 
    if ((u_char *) a->elts + a->size * a->nalloc == p->d.last) {
        p->d.last -= a->size * a->nalloc;
    }
//if the array struct is at the end of poll.return it 
    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        p->d.last = (u_char *) a;
    }
}


void *
ngx_array_push(ngx_array_t *a)
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;

    if (a->nelts == a->nalloc) {//���磬������
        /* the array is full */
        size = a->size * a->nalloc;//����һ�����ڵ��ܴ�С
        p = a->pool;
        if ((u_char *) a->elts + size == p->d.last && p->d.last + a->size <= p->d.end)
        {//��ѽ������������pool�����һ����Ա�������Һ���Ŀյ��㹻�ݵ���һ��Ԫ�صĴ�С��ֱ����չ֮!�������������伫��
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += a->size;//pool����ֱ�Ӹ��һ����ɣ��Ҷ���
            a->nalloc++;//����������1

        } else {
            /* allocate a new array */

            new = ngx_palloc(p, 2 * size);//û�취�ˣ����ڴ�������2����С�ġ�
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, size);
            a->elts = new;//����ôָ�����µ��ڴ棬�ɵ��ڴ�Ͳ��ͷ����������nginx�ڴ��������ͨ���ڴ������ģ�����ʾ�ͷţ�ͳһ�ͷš�
            a->nalloc *= 2;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts++;
//return the address of the pushed element
    return elt;
}


void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size;
    ngx_uint_t   nalloc;
    ngx_pool_t  *p;

    size = n * a->size;
    if (a->nelts + n > a->nalloc) {
        /* the array is full */
        p = a->pool;
        if ((u_char *) a->elts + a->size * a->nalloc == p->d.last && p->d.last + size <= p->d.end) {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */
            p->d.last += size;
            a->nalloc += n;
        } else {
            /* allocate a new array */
            nalloc = 2 * ((n >= a->nalloc) ? n : a->nalloc);//��������2������Ŀ
            new = ngx_palloc(p, nalloc * a->size);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, a->nelts * a->size);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
