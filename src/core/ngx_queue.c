
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * find the middle queue element if the queue has odd number of elements
 * or the first element of the queue's second part otherwise
 */
ngx_queue_t *
ngx_queue_middle(ngx_queue_t *queue)
{//�õ��������в�λ��
    ngx_queue_t  *middle, *next;

    middle = ngx_queue_head(queue);
    if (middle == ngx_queue_last(queue)) {
        return middle;//��һ���ڵ㡣
    }
    next = ngx_queue_head(queue);
    for ( ;; ) {//middleָ��ÿ��ѭ����һ����nextÿ����2�����Ǿ͵�next�����յ��ʱ��middleָ�����õ����м�ĵط���
        middle = 	ngx_queue_next(middle);
        next = 		ngx_queue_next(next);
        if (next == ngx_queue_last(queue)) {
            return middle;
        }
        next = ngx_queue_next(next);
        if (next == ngx_queue_last(queue)) {
            return middle;
        }
    }
}


/* the stable insertion sort */
void
ngx_queue_sort(ngx_queue_t *queue, ngx_int_t (*cmp)(const ngx_queue_t *, const ngx_queue_t *))
{//��С�����������
    ngx_queue_t  *q, *prev, *next;

    q = ngx_queue_head(queue);
    if (q == ngx_queue_last(queue)) {
        return;//��һ���ڵ㡣
    }
    for (q = ngx_queue_next(q); q != ngx_queue_sentinel(queue); q = next) {
        prev = ngx_queue_prev(q);
        next = ngx_queue_next(q);
        ngx_queue_remove(q);
        do {
            if (cmp(prev, q) <= 0) {
                break;//���ǰ���С���Ǿ�û���ˡ�
            }
            prev = ngx_queue_prev(prev);
        } while (prev != ngx_queue_sentinel(queue));
        ngx_queue_insert_after(prev, q);//������档
    }
}