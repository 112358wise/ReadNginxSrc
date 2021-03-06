
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


//以下数据结构参考http://cjhust.blog.163.com/blog/static/17582715720111017114121678/
typedef struct {//用来存放每个客户端节点的相关信息。
    u_char                       color;
    u_char                       dummy;
    u_short                      len;//data字符串的长度。
    ngx_queue_t                  queue;
    ngx_msec_t                   last;//这个节点什么时候加入的。
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   excess;//超过，超额量，多余量。 1等价于0.001 r/s
    u_char                       data[1];
} ngx_http_limit_req_node_t;


typedef struct {//管理客户端节点的信息。这个是由共享内存块的data指向的，看起名字就知道了。ngx_shm_zone_t->data
    ngx_rbtree_t                  rbtree;//红黑树的根。
    ngx_rbtree_node_t             sentinel;
    ngx_queue_t                   queue;//用来记录不同时间段进来的请求，队列操作，前面进，后面出。
} ngx_http_limit_req_shctx_t;


typedef struct {//该结构用于存放limit_req_zone指令的相关信息
    ngx_http_limit_req_shctx_t  *sh;//指向上面的红黑树，队列结构
    ngx_slab_pool_t             *shpool;//指向slab池。实际为(ngx_slab_pool_t *) shm_zone->shm.addr;
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   rate;//每秒的请求量*1000。
    ngx_int_t                    index;//变量在 cmcf->variables.nelts中的下标。
    ngx_str_t                    var;//value[i];//记住变量的名字。 存放具体的$binary_remote_addr
} ngx_http_limit_req_ctx_t;


typedef struct {//该结构用于存放limit_req指令的相关信息
    ngx_shm_zone_t              *shm_zone;//ngx_shared_memory_add返回的指针
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   burst;//每秒鬓发数目*1000.
    ngx_uint_t                   limit_log_level;
    ngx_uint_t                   delay_log_level;

    ngx_uint_t                   nodelay; /* unsigned  nodelay:1 */
} ngx_http_limit_req_conf_t;


static void ngx_http_limit_req_delay(ngx_http_request_t *r);
static ngx_int_t ngx_http_limit_req_lookup(ngx_http_limit_req_conf_t *lrcf,
    ngx_uint_t hash, u_char *data, size_t len, ngx_uint_t *ep);
static void ngx_http_limit_req_expire(ngx_http_limit_req_ctx_t *ctx,
    ngx_uint_t n);

static void *ngx_http_limit_req_create_conf(ngx_conf_t *cf);
static char *ngx_http_limit_req_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_limit_req_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_limit_req(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_http_limit_req_init(ngx_conf_t *cf);


static ngx_conf_enum_t  ngx_http_limit_req_log_levels[] = {
    { ngx_string("info"), NGX_LOG_INFO },
    { ngx_string("notice"), NGX_LOG_NOTICE },
    { ngx_string("warn"), NGX_LOG_WARN },
    { ngx_string("error"), NGX_LOG_ERR },
    { ngx_null_string, 0 }
};

//limit_req_zone $variable zone=name:size rate=rate;
//limit_req zone=name [burst=number] [nodelay];
static ngx_command_t  ngx_http_limit_req_commands[] = {

    { ngx_string("limit_req_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE3,
      ngx_http_limit_req_zone,
      0,
      0,
      NULL },

    { ngx_string("limit_req"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_http_limit_req,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("limit_req_log_level"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_limit_req_conf_t, limit_log_level),
      &ngx_http_limit_req_log_levels 
    },
      ngx_null_command
};


static ngx_http_module_t  ngx_http_limit_req_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_limit_req_init,               /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_limit_req_create_conf,        /* create location configration */
    ngx_http_limit_req_merge_conf          /* merge location configration */
};


ngx_module_t  ngx_http_limit_req_module = {
    NGX_MODULE_V1,
    &ngx_http_limit_req_module_ctx,        /* module context */
    ngx_http_limit_req_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t ngx_http_limit_req_handler(ngx_http_request_t *r)
{//请求频率限制处理过程函数。下面根据客户端的IP获取其访问频率信息。确定是否允许连接还是延迟。
    size_t                      len, n;
    uint32_t                    hash;
    ngx_int_t                   rc;
    ngx_uint_t                  excess;//1秒的余量
    ngx_time_t                 *tp;
    ngx_rbtree_node_t          *node;
    ngx_http_variable_value_t  *vv;
    ngx_http_limit_req_ctx_t   *ctx;
    ngx_http_limit_req_node_t  *lr;
    ngx_http_limit_req_conf_t  *lrcf;

    if (r->main->limit_req_set) {
        return NGX_DECLINED;
    }
    lrcf = ngx_http_get_module_loc_conf(r, ngx_http_limit_req_module);
    if (lrcf->shm_zone == NULL) {
        return NGX_DECLINED;
    }
    ctx = lrcf->shm_zone->data;//data为ngx_http_limit_req_zone设置的ngx_http_limit_req_ctx_t接口，里面记录了这个session的名字，大小等。
    vv = ngx_http_get_indexed_variable(r, ctx->index);//根据这个zon在总的variables数组中的下标，找到其值，其中可能需要计算一下。get_handler
    //vv 为&r->variables[index];的一项。
    if (vv == NULL || vv->not_found) {
        return NGX_DECLINED;
    }
    len = vv->len;
    if (len == 0) {
        return NGX_DECLINED;
    }
    if (len > 65535) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "the value of the \"%V\" variable is more than 65535 bytes: \"%v\"",&ctx->var, vv);
        return NGX_DECLINED;
    }

    r->main->limit_req_set = 1;
    hash = ngx_crc32_short(vv->data, len);
	
    ngx_shmtx_lock(&ctx->shpool->mutex);
    ngx_http_limit_req_expire(ctx, 1);//删除60秒之外的队列项。
    rc = ngx_http_limit_req_lookup(lrcf, hash, vv->data, len, &excess);

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "limit_req: %i %ui.%03ui", rc, excess / 1000, excess % 1000);
    if (rc == NGX_DECLINED) {//在红黑树上没有找到，所以需要增加一个进去了。
        n = offsetof(ngx_rbtree_node_t, color) + offsetof(ngx_http_limit_req_node_t, data) + len;

        node = ngx_slab_alloc_locked(ctx->shpool, n);//在共享内存中申请一个节点
        if (node == NULL) {//如果失败，就尝试释放一些旧的节点。
            ngx_http_limit_req_expire(ctx, 0);//去掉三个60分钟之外的
            node = ngx_slab_alloc_locked(ctx->shpool, n);
            if (node == NULL) {
                ngx_shmtx_unlock(&ctx->shpool->mutex);
                return NGX_HTTP_SERVICE_UNAVAILABLE;
            }
        }
		//准备一个新的请求结构，加入到红黑树里面。
        lr = (ngx_http_limit_req_node_t *) &node->color;//得到首地址。
        node->key = hash;
        lr->len = (u_char) len;
        tp = ngx_timeofday();
        lr->last = (ngx_msec_t) (tp->sec * 1000 + tp->msec);//当前秒数
        lr->excess = 0;
        ngx_memcpy(lr->data, vv->data, len);

        ngx_rbtree_insert(&ctx->sh->rbtree, node);//将新节点加入到红黑树
        ngx_queue_insert_head(&ctx->sh->queue, &lr->queue);//加入到队列的头部，表示是最新的数据。
        ngx_shmtx_unlock(&ctx->shpool->mutex);
        return NGX_DECLINED;
    }

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    if (rc == NGX_OK) {//OK，没有超过设定值，可以访问
        return NGX_DECLINED;
    }
    if (rc == NGX_BUSY) {//访问平率超过了限制。
        ngx_log_error(lrcf->limit_log_level, r->connection->log, 0,
                      "limiting requests, excess: %ui.%03ui by zone \"%V\"", excess / 1000, excess % 1000, &lrcf->shm_zone->shm.name);
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }
    /* rc == NGX_AGAIN */
    if (lrcf->nodelay) {//如果设置了nodelay，不要拖请求，就直接返回，停止这个连接。
        return NGX_DECLINED;
    }

    ngx_log_error(lrcf->delay_log_level, r->connection->log, 0,
                  "delaying request, excess: %ui.%03ui, by zone \"%V\"", excess / 1000, excess % 1000, &lrcf->shm_zone->shm.name);

    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {//删除可读事件，这样就不会care这个连接的事件了
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

	//设置一个可写超时，然后到那时候就重新跑这个连接。从ngx_http_limit_req_delay开始。
    r->read_event_handler = ngx_http_test_reading;
    r->write_event_handler = ngx_http_limit_req_delay;
    ngx_add_timer(r->connection->write, (ngx_msec_t) excess * 1000 / ctx->rate);//依靠剩余的来决定超时时间。

    return NGX_AGAIN;
}


static void
ngx_http_limit_req_delay(ngx_http_request_t *r)
{//ngx_http_limit_req_handler处理过程设置的延迟回调，定时器到来后就调用这里，客户端请求被延迟了这么久。
    ngx_event_t  *wev;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "limit_req delay");

    wev = r->connection->write;
    if (!wev->timedout) {//超时了，将可写事件删除，这是为什么 �
        if (ngx_handle_write_event(wev, 0) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
        return;
    }

    wev->timedout = 0;
    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {//将可读事件删除，不关注了，这也是什么原因?
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    r->read_event_handler = ngx_http_block_reading;
    r->write_event_handler = ngx_http_core_run_phases;//重新设置回ngx_http_core_run_phases，这样又可以进入处理过程循环中了。
    ngx_http_core_run_phases(r);//手动进入处理过程。
}


static void
ngx_http_limit_req_rbtree_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t          **p;
    ngx_http_limit_req_node_t   *lrn, *lrnt;

    for ( ;; ) {
        if (node->key < temp->key) {
            p = &temp->left;
        } else if (node->key > temp->key) {
            p = &temp->right;
        } else { /* node->key == temp->key */
            lrn = (ngx_http_limit_req_node_t *) &node->color;
            lrnt = (ngx_http_limit_req_node_t *) &temp->color;
            p = (ngx_memn2cmp(lrn->data, lrnt->data, lrn->len, lrnt->len) < 0)
                ? &temp->left : &temp->right;
        }
        if (*p == sentinel) {
            break;
        }
        temp = *p;
    }
    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


/*（1）NGX_ DECLINED：节点不存在；
（2）NGX_OK：该客户端访问频率未超过设定值；
（3）NGX_AGAIN：该客户端访问频率超过了设定值，但是并未超过阈值（与burst有关）；
（4）NGX_BUSY：该客户端访问频率超过了阈值；*/
static ngx_int_t
ngx_http_limit_req_lookup(ngx_http_limit_req_conf_t *lrcf, ngx_uint_t hash, u_char *data, size_t len, ngx_uint_t *ep)
{//在红黑树中查找指定hash，名字的节点，返回是否超过了阈值。
//个人感觉这个令牌桶算法有的绌劣，控制不准，大概的思想是: 每秒为每个IP分配x个请求，同时要求某一秒最多不能超过y个请求；
//然后第一秒用了x+1个请求，第二秒就只能用x-1个了。某一秒最多用的请求数为y(burst)
    ngx_int_t                   rc, excess;
    ngx_time_t                 *tp;
    ngx_msec_t                  now;
    ngx_msec_int_t              ms;
    ngx_rbtree_node_t          *node, *sentinel;
    ngx_http_limit_req_ctx_t   *ctx;
    ngx_http_limit_req_node_t  *lr;//用来存放每个客户端节点的相关信息。

    ctx = lrcf->shm_zone->data;
    node = ctx->sh->rbtree.root;//红黑树的根。
    sentinel = ctx->sh->rbtree.sentinel;
    while (node != sentinel) {
        if (hash < node->key) {//目标小于当前，左边；
            node = node->left;
            continue;
        }
        if (hash > node->key) {//右边
            node = node->right;
            continue;
        }
        /* hash == node->key */
        do {
            lr = (ngx_http_limit_req_node_t *) &node->color;//拿到首地址
            rc = ngx_memn2cmp(data, lr->data, len, (size_t) lr->len);
            if (rc == 0) {//名字确实相同。OK，就是了
                ngx_queue_remove(&lr->queue);//把这个节点先删除，然后插入到队列头部。表示他更新了。
                ngx_queue_insert_head(&ctx->sh->queue, &lr->queue);
                tp = ngx_timeofday();
                now = (ngx_msec_t) (tp->sec * 1000 + tp->msec);
                ms = (ngx_msec_int_t) (now - lr->last);//得到这个节点已经过了多久。
                /*注意下面等式，有2个地方要注意:
                1. +1000的意思是，这次有个新的连接来了，计数应该增加1000.以备后面判断是否有超过阈值
                2. ctx->rate * ngx_abs(ms)的原因是: 假如这个IP5秒没有访问过了，那么就算它没有访问，但是配额我们是给他准备，并被浪费了
                	我们也应该减去这5分钟以来为他准备的配额，不管他到底用了多少(lr->excess)。减少为0是没事的。
                	如果ms为0，也就是这一秒同一个IP来了2次，那么lr->excess相当于增加了2000，如果2000> burst，则并发太高拒绝；
                	lr->excess代表的是累积的，这个IP一共放了多少个请求出去。
                */
                excess = lr->excess - ctx->rate * ngx_abs(ms) / 1000 + 1000;//每一个请求，都增加1000，然后减去旧的。
                if (excess < 0) {
                    excess = 0;//如果小于0，改为0，丢掉没用的配额了。
                }
                *ep = excess;//返回参数，设置为当前还存在多少

                if ((ngx_uint_t) excess > lrcf->burst) {//如果当前存在队列总的数目，返回表明已经很忙了，不能放了。
                    return NGX_BUSY;
                }

                lr->excess = excess;//记住到目前为止，使用的数目。
                lr->last = now;//我这个节点是当前这个时候放入的。
                if (excess) {//还好没有超过burst数目，但是需要等待或者nodelay的话得503了。
                    return NGX_AGAIN;
                }
                return NGX_OK;
            }
            node = (rc < 0) ? node->left : node->right;//决定左走还是右走。
        } while (node != sentinel && hash == node->key);

        break;
    }
    *ep = 0;
    return NGX_DECLINED;
}


static void
ngx_http_limit_req_expire(ngx_http_limit_req_ctx_t *ctx, ngx_uint_t n)
{//删除队列和红黑树里面时间超过1分钟的节点。从队列的尾部取就行，不断的取，直到取到的节点时间在60秒之内为止，然后退出。
    ngx_int_t                   excess;
    ngx_time_t                 *tp;
    ngx_msec_t                  now;
    ngx_queue_t                *q;
    ngx_msec_int_t              ms;
    ngx_rbtree_node_t          *node;
    ngx_http_limit_req_node_t  *lr;

    tp = ngx_timeofday();//带缓存读取时间值。
    now = (ngx_msec_t) (tp->sec * 1000 + tp->msec);//当前秒数
    /*
     * n == 1 deletes one or two zero rate entries
     * n == 0 deletes oldest entry by force
     *        and one or two zero rate entries
     */

    while (n < 3) {
        if (ngx_queue_empty(&ctx->sh->queue)) {//如果队列里面没东西，就不用删除。
            return;
        }
        q = ngx_queue_last(&ctx->sh->queue);//否则取队列的最后一个项，拿到其设置的时间
        lr = ngx_queue_data(q, ngx_http_limit_req_node_t, queue);
        if (n++ != 0) {
            ms = (ngx_msec_int_t) (now - lr->last);//计算时间差，跟当前的时间差。
            ms = ngx_abs(ms);//相差60秒
            if (ms < 60000) {
                return;
            }
            excess = lr->excess - ctx->rate * ms / 1000;//这是啥意思
            if (excess > 0) {
                return;
            }
        }
		//将这个过时的节点从队列，红黑树中删除。然后解锁
        ngx_queue_remove(q);
        node = (ngx_rbtree_node_t *) ((u_char *) lr - offsetof(ngx_rbtree_node_t, color));
        ngx_rbtree_delete(&ctx->sh->rbtree, node);
	
        ngx_slab_free_locked(ctx->shpool, node);
    }
}


static ngx_int_t
ngx_http_limit_req_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{//初始化限流用的共享内存，红黑树，队列。ctx->sh和ctx->shpool
    ngx_http_limit_req_ctx_t  *octx = data;
    size_t                     len;
    ngx_http_limit_req_ctx_t  *ctx;

    ctx = shm_zone->data;
    if (octx) {//reload的时候也会调用这里，这个时候data不为空。
        if (ngx_strcmp(ctx->var.data, octx->var.data) != 0) {
            ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                          "limit_req \"%V\" uses the \"%V\" variable while previously it used the \"%V\" variable",
                          &shm_zone->shm.name, &ctx->var, &octx->var);
            return NGX_ERROR;
        }
		//由于octx是本地内存中分配的，也是在old_cycle中分配的，所以需要在新的ctx中重新初始化一下  
        // 所以这里只是关于本地内存的重新初始化，而关于共享内存中的初始化工作就不需要再做了  
        ctx->sh = octx->sh;
        ctx->shpool = octx->shpool;
        return NGX_OK;
    }

    ctx->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    if (shm_zone->shm.exists) {
        ctx->sh = ctx->shpool->data;
        return NGX_OK;
    }
    ctx->sh = ngx_slab_alloc(ctx->shpool, sizeof(ngx_http_limit_req_shctx_t));
    if (ctx->sh == NULL) {
        return NGX_ERROR;
    }
    ctx->shpool->data = ctx->sh;

    ngx_rbtree_init(&ctx->sh->rbtree, &ctx->sh->sentinel, ngx_http_limit_req_rbtree_insert_value);
    ngx_queue_init(&ctx->sh->queue);
    len = sizeof(" in limit_req zone \"\"") + shm_zone->shm.name.len;

    ctx->shpool->log_ctx = ngx_slab_alloc(ctx->shpool, len);
    if (ctx->shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_sprintf(ctx->shpool->log_ctx, " in limit_req zone \"%V\"%Z", &shm_zone->shm.name);
    return NGX_OK;
}


static void *
ngx_http_limit_req_create_conf(ngx_conf_t *cf)
{
    ngx_http_limit_req_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_limit_req_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    /*
     * set by ngx_pcalloc():
     *     conf->shm_zone = NULL;
     *     conf->burst = 0;
     *     conf->nodelay = 0;
     */
    conf->limit_log_level = NGX_CONF_UNSET_UINT;
    return conf;
}


static char *
ngx_http_limit_req_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_limit_req_conf_t *prev = parent;
    ngx_http_limit_req_conf_t *conf = child;

    if (conf->shm_zone == NULL) {
        *conf = *prev;
    }
    ngx_conf_merge_uint_value(conf->limit_log_level, prev->limit_log_level, NGX_LOG_ERR);
    conf->delay_log_level = (conf->limit_log_level == NGX_LOG_INFO) ? NGX_LOG_INFO : conf->limit_log_level + 1;
    return NGX_CONF_OK;
}


//limit_req_zone $variable zone=name:size rate=rate;
//http://nginx.org/en/docs/http/ngx_http_limit_req_module.html
static char * ngx_http_limit_req_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//碰到"limit_req_zone"指令的时候调用这里。
    u_char                    *p;
    size_t                     size, len;
    ngx_str_t                 *value, name, s;
    ngx_int_t                  rate, scale;//scale为时间单位是每秒还是每分钟
    ngx_uint_t                 i;
    ngx_shm_zone_t            *shm_zone;
    ngx_http_limit_req_ctx_t  *ctx;

    value = cf->args->elts;

    ctx = NULL;
    size = 0;
    rate = 1;
    scale = 1;
    name.len = 0;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {//zone=name:size
            name.data = value[i].data + 5;
            p = (u_char *) ngx_strchr(name.data, ':');//找到name:size
            if (p) {
                *p = '\0';
                name.len = p - name.data;
                p++;
                s.len = value[i].data + value[i].len - p;
                s.data = p;//大小字段

                size = ngx_parse_size(&s);//解析一下大小，返回字节数。
                if (size > 8191) {
                    continue;
                }
            }
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid zone size \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }

        if (ngx_strncmp(value[i].data, "rate=", 5) == 0) {//rate=rate;
            len = value[i].len;
            p = value[i].data + len - 3;
            if (ngx_strncmp(p, "r/s", 3) == 0) {
                scale = 1;
                len -= 3;
            } else if (ngx_strncmp(p, "r/m", 3) == 0) {
                scale = 60;
                len -= 3;
            }
            rate = ngx_atoi(value[i].data + 5, len - 5);
            if (rate <= NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid rate \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }

        if (value[i].data[0] == '$') {//$variable 
            value[i].len--;//需要解析变量
            value[i].data++;
            ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_limit_req_ctx_t));
            if (ctx == NULL) {
                return NGX_CONF_ERROR;
            }
			//根据变量名字，查找或者添加一个项，在cmcf->variables.nelts 里面。不过其set/get_handler还没有设置好。为空
            ctx->index = ngx_http_get_variable_index(cf, &value[i]);//传入变量的名字，获取其在配置里面的下标。
            if (ctx->index == NGX_ERROR) {
                return NGX_CONF_ERROR;
            }
            ctx->var = value[i];//记住变量的名字。
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (name.len == 0 || size == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"%V\" must have \"zone\" parameter", &cmd->name);
        return NGX_CONF_ERROR;
    }
    if (ctx == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no variable is defined for limit_req_zone \"%V\"", &cmd->name);
        return NGX_CONF_ERROR;
    }
    ctx->rate = rate * 1000 / scale;//算出每分秒的量请求量。
	//下面根据名字，申请一块共享内存，大小为size。并记录init初始化函数，记录ngx_http_limit_req_ctx_t的上下文结构。
    shm_zone = ngx_shared_memory_add(cf, &name, size, &ngx_http_limit_req_module);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }
    if (shm_zone->data) {
        ctx = shm_zone->data;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"limit_req_zone \"%V\" is already bound to variable \"%V\"", &value[1], &ctx->var);
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_limit_req_init_zone;
    shm_zone->data = ctx;//记录刚申请准备的ctx，里面记录了rate,index,var等信息。
    return NGX_CONF_OK;
}

//limit_req zone=name [burst=number] [nodelay];
static char * ngx_http_limit_req(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_limit_req_conf_t  *lrcf = conf;

    ngx_int_t    burst;
    ngx_str_t   *value, s;
    ngx_uint_t   i;

    if (lrcf->shm_zone) {
        return "is duplicate";
    }
    value = cf->args->elts;
    burst = 0;
    for (i = 1; i < cf->args->nelts; i++) {
        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {//记录名字。
            s.len = value[i].len - 5;
            s.data = value[i].data + 5;
			//查找一个已经存在名字的，或者新申请一个共享内存结构，存入&cf->cycle->shared_memory.part里面。
            lrcf->shm_zone = ngx_shared_memory_add(cf, &s, 0, &ngx_http_limit_req_module);
            if (lrcf->shm_zone == NULL) {
                return NGX_CONF_ERROR;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "burst=", 6) == 0) {//突发数目。
            burst = ngx_atoi(value[i].data + 6, value[i].len - 6);
            if (burst <= 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid burst rate \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "nodelay", 7) == 0) {
            lrcf->nodelay = 1;//设置了nodelay标志，这样过载的请求会直接返回503而不犹豫。
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (lrcf->shm_zone == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"%V\" must have \"zone\" parameter", &cmd->name);
        return NGX_CONF_ERROR;
    }

    if (lrcf->shm_zone->data == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unknown limit_req_zone \"%V\"", &lrcf->shm_zone->shm.name);
        return NGX_CONF_ERROR;
    }
    lrcf->burst = burst * 1000;
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_limit_req_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);//在PREACCESS过程之前申请一个槽位，增加这个handler
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_limit_req_handler;//注册这个函数为phrase过滤函数，请求在content phrase之前会调用这里进行访问频度限制。
    return NGX_OK;
}
