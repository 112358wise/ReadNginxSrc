
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


//�������ݽṹ�ο�http://cjhust.blog.163.com/blog/static/17582715720111017114121678/
typedef struct {//�������ÿ���ͻ��˽ڵ�������Ϣ��
    u_char                       color;
    u_char                       dummy;
    u_short                      len;//data�ַ����ĳ��ȡ�
    ngx_queue_t                  queue;
    ngx_msec_t                   last;//����ڵ�ʲôʱ�����ġ�
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   excess;//���������������������� 1�ȼ���0.001 r/s
    u_char                       data[1];
} ngx_http_limit_req_node_t;


typedef struct {//����ͻ��˽ڵ����Ϣ��������ɹ����ڴ���dataָ��ģ��������־�֪���ˡ�ngx_shm_zone_t->data
    ngx_rbtree_t                  rbtree;//������ĸ���
    ngx_rbtree_node_t             sentinel;
    ngx_queue_t                   queue;//������¼��ͬʱ��ν��������󣬶��в�����ǰ������������
} ngx_http_limit_req_shctx_t;


typedef struct {//�ýṹ���ڴ��limit_req_zoneָ��������Ϣ
    ngx_http_limit_req_shctx_t  *sh;//ָ������ĺ���������нṹ
    ngx_slab_pool_t             *shpool;//ָ��slab�ء�ʵ��Ϊ(ngx_slab_pool_t *) shm_zone->shm.addr;
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   rate;//ÿ���������*1000��
    ngx_int_t                    index;//������ cmcf->variables.nelts�е��±ꡣ
    ngx_str_t                    var;//value[i];//��ס���������֡� ��ž����$binary_remote_addr
} ngx_http_limit_req_ctx_t;


typedef struct {//�ýṹ���ڴ��limit_reqָ��������Ϣ
    ngx_shm_zone_t              *shm_zone;//ngx_shared_memory_add���ص�ָ��
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   burst;//ÿ���޷���Ŀ*1000.
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
{//����Ƶ�����ƴ�����̺�����������ݿͻ��˵�IP��ȡ�����Ƶ����Ϣ��ȷ���Ƿ��������ӻ����ӳ١�
    size_t                      len, n;
    uint32_t                    hash;
    ngx_int_t                   rc;
    ngx_uint_t                  excess;//1�������
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
    ctx = lrcf->shm_zone->data;//dataΪngx_http_limit_req_zone���õ�ngx_http_limit_req_ctx_t�ӿڣ������¼�����session�����֣���С�ȡ�
    vv = ngx_http_get_indexed_variable(r, ctx->index);//�������zon���ܵ�variables�����е��±꣬�ҵ���ֵ�����п�����Ҫ����һ�¡�get_handler
    //vv Ϊ&r->variables[index];��һ�
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
    ngx_http_limit_req_expire(ctx, 1);//ɾ��60��֮��Ķ����
    rc = ngx_http_limit_req_lookup(lrcf, hash, vv->data, len, &excess);

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "limit_req: %i %ui.%03ui", rc, excess / 1000, excess % 1000);
    if (rc == NGX_DECLINED) {//�ں������û���ҵ���������Ҫ����һ����ȥ�ˡ�
        n = offsetof(ngx_rbtree_node_t, color) + offsetof(ngx_http_limit_req_node_t, data) + len;

        node = ngx_slab_alloc_locked(ctx->shpool, n);//�ڹ����ڴ�������һ���ڵ�
        if (node == NULL) {//���ʧ�ܣ��ͳ����ͷ�һЩ�ɵĽڵ㡣
            ngx_http_limit_req_expire(ctx, 0);//ȥ������60����֮���
            node = ngx_slab_alloc_locked(ctx->shpool, n);
            if (node == NULL) {
                ngx_shmtx_unlock(&ctx->shpool->mutex);
                return NGX_HTTP_SERVICE_UNAVAILABLE;
            }
        }
		//׼��һ���µ�����ṹ�����뵽��������档
        lr = (ngx_http_limit_req_node_t *) &node->color;//�õ��׵�ַ��
        node->key = hash;
        lr->len = (u_char) len;
        tp = ngx_timeofday();
        lr->last = (ngx_msec_t) (tp->sec * 1000 + tp->msec);//��ǰ����
        lr->excess = 0;
        ngx_memcpy(lr->data, vv->data, len);

        ngx_rbtree_insert(&ctx->sh->rbtree, node);//���½ڵ���뵽�����
        ngx_queue_insert_head(&ctx->sh->queue, &lr->queue);//���뵽���е�ͷ������ʾ�����µ����ݡ�
        ngx_shmtx_unlock(&ctx->shpool->mutex);
        return NGX_DECLINED;
    }

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    if (rc == NGX_OK) {//OK��û�г����趨ֵ�����Է���
        return NGX_DECLINED;
    }
    if (rc == NGX_BUSY) {//����ƽ�ʳ��������ơ�
        ngx_log_error(lrcf->limit_log_level, r->connection->log, 0,
                      "limiting requests, excess: %ui.%03ui by zone \"%V\"", excess / 1000, excess % 1000, &lrcf->shm_zone->shm.name);
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }
    /* rc == NGX_AGAIN */
    if (lrcf->nodelay) {//���������nodelay����Ҫ�����󣬾�ֱ�ӷ��أ�ֹͣ������ӡ�
        return NGX_DECLINED;
    }

    ngx_log_error(lrcf->delay_log_level, r->connection->log, 0,
                  "delaying request, excess: %ui.%03ui, by zone \"%V\"", excess / 1000, excess % 1000, &lrcf->shm_zone->shm.name);

    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {//ɾ���ɶ��¼��������Ͳ���care������ӵ��¼���
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

	//����һ����д��ʱ��Ȼ����ʱ���������������ӡ���ngx_http_limit_req_delay��ʼ��
    r->read_event_handler = ngx_http_test_reading;
    r->write_event_handler = ngx_http_limit_req_delay;
    ngx_add_timer(r->connection->write, (ngx_msec_t) excess * 1000 / ctx->rate);//����ʣ�����������ʱʱ�䡣

    return NGX_AGAIN;
}


static void
ngx_http_limit_req_delay(ngx_http_request_t *r)
{//ngx_http_limit_req_handler����������õ��ӳٻص�����ʱ��������͵�������ͻ��������ӳ�����ô�á�
    ngx_event_t  *wev;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "limit_req delay");

    wev = r->connection->write;
    if (!wev->timedout) {//��ʱ�ˣ�����д�¼�ɾ��������Ϊʲô �
        if (ngx_handle_write_event(wev, 0) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
        return;
    }

    wev->timedout = 0;
    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {//���ɶ��¼�ɾ��������ע�ˣ���Ҳ��ʲôԭ��?
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    r->read_event_handler = ngx_http_block_reading;
    r->write_event_handler = ngx_http_core_run_phases;//�������û�ngx_http_core_run_phases�������ֿ��Խ��봦�����ѭ�����ˡ�
    ngx_http_core_run_phases(r);//�ֶ����봦����̡�
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


/*��1��NGX_ DECLINED���ڵ㲻���ڣ�
��2��NGX_OK���ÿͻ��˷���Ƶ��δ�����趨ֵ��
��3��NGX_AGAIN���ÿͻ��˷���Ƶ�ʳ������趨ֵ�����ǲ�δ������ֵ����burst�йأ���
��4��NGX_BUSY���ÿͻ��˷���Ƶ�ʳ�������ֵ��*/
static ngx_int_t
ngx_http_limit_req_lookup(ngx_http_limit_req_conf_t *lrcf, ngx_uint_t hash, u_char *data, size_t len, ngx_uint_t *ep)
{//�ں�����в���ָ��hash�����ֵĽڵ㣬�����Ƿ񳬹�����ֵ��
//���˸о��������Ͱ�㷨�е���ӣ����Ʋ�׼����ŵ�˼����: ÿ��Ϊÿ��IP����x������ͬʱҪ��ĳһ����಻�ܳ���y������
//Ȼ���һ������x+1�����󣬵ڶ����ֻ����x-1���ˡ�ĳһ������õ�������Ϊy(burst)
    ngx_int_t                   rc, excess;
    ngx_time_t                 *tp;
    ngx_msec_t                  now;
    ngx_msec_int_t              ms;
    ngx_rbtree_node_t          *node, *sentinel;
    ngx_http_limit_req_ctx_t   *ctx;
    ngx_http_limit_req_node_t  *lr;//�������ÿ���ͻ��˽ڵ�������Ϣ��

    ctx = lrcf->shm_zone->data;
    node = ctx->sh->rbtree.root;//������ĸ���
    sentinel = ctx->sh->rbtree.sentinel;
    while (node != sentinel) {
        if (hash < node->key) {//Ŀ��С�ڵ�ǰ����ߣ�
            node = node->left;
            continue;
        }
        if (hash > node->key) {//�ұ�
            node = node->right;
            continue;
        }
        /* hash == node->key */
        do {
            lr = (ngx_http_limit_req_node_t *) &node->color;//�õ��׵�ַ
            rc = ngx_memn2cmp(data, lr->data, len, (size_t) lr->len);
            if (rc == 0) {//����ȷʵ��ͬ��OK��������
                ngx_queue_remove(&lr->queue);//������ڵ���ɾ����Ȼ����뵽����ͷ������ʾ�������ˡ�
                ngx_queue_insert_head(&ctx->sh->queue, &lr->queue);
                tp = ngx_timeofday();
                now = (ngx_msec_t) (tp->sec * 1000 + tp->msec);
                ms = (ngx_msec_int_t) (now - lr->last);//�õ�����ڵ��Ѿ����˶�á�
                /*ע�������ʽ����2���ط�Ҫע��:
                1. +1000����˼�ǣ�����и��µ��������ˣ�����Ӧ������1000.�Ա������ж��Ƿ��г�����ֵ
                2. ctx->rate * ngx_abs(ms)��ԭ����: �������IP5��û�з��ʹ��ˣ���ô������û�з��ʣ�������������Ǹ���׼���������˷���
                	����ҲӦ�ü�ȥ��5��������Ϊ��׼�������������������˶���(lr->excess)������Ϊ0��û�µġ�
                	���msΪ0��Ҳ������һ��ͬһ��IP����2�Σ���ôlr->excess�൱��������2000�����2000> burst���򲢷�̫�߾ܾ���
                	lr->excess��������ۻ��ģ����IPһ�����˶��ٸ������ȥ��
                */
                excess = lr->excess - ctx->rate * ngx_abs(ms) / 1000 + 1000;//ÿһ�����󣬶�����1000��Ȼ���ȥ�ɵġ�
                if (excess < 0) {
                    excess = 0;//���С��0����Ϊ0������û�õ�����ˡ�
                }
                *ep = excess;//���ز���������Ϊ��ǰ�����ڶ���

                if ((ngx_uint_t) excess > lrcf->burst) {//�����ǰ���ڶ����ܵ���Ŀ�����ر����Ѿ���æ�ˣ����ܷ��ˡ�
                    return NGX_BUSY;
                }

                lr->excess = excess;//��ס��ĿǰΪֹ��ʹ�õ���Ŀ��
                lr->last = now;//������ڵ��ǵ�ǰ���ʱ�����ġ�
                if (excess) {//����û�г���burst��Ŀ��������Ҫ�ȴ�����nodelay�Ļ���503�ˡ�
                    return NGX_AGAIN;
                }
                return NGX_OK;
            }
            node = (rc < 0) ? node->left : node->right;//�������߻������ߡ�
        } while (node != sentinel && hash == node->key);

        break;
    }
    *ep = 0;
    return NGX_DECLINED;
}


static void
ngx_http_limit_req_expire(ngx_http_limit_req_ctx_t *ctx, ngx_uint_t n)
{//ɾ�����кͺ��������ʱ�䳬��1���ӵĽڵ㡣�Ӷ��е�β��ȡ���У����ϵ�ȡ��ֱ��ȡ���Ľڵ�ʱ����60��֮��Ϊֹ��Ȼ���˳���
    ngx_int_t                   excess;
    ngx_time_t                 *tp;
    ngx_msec_t                  now;
    ngx_queue_t                *q;
    ngx_msec_int_t              ms;
    ngx_rbtree_node_t          *node;
    ngx_http_limit_req_node_t  *lr;

    tp = ngx_timeofday();//�������ȡʱ��ֵ��
    now = (ngx_msec_t) (tp->sec * 1000 + tp->msec);//��ǰ����
    /*
     * n == 1 deletes one or two zero rate entries
     * n == 0 deletes oldest entry by force
     *        and one or two zero rate entries
     */

    while (n < 3) {
        if (ngx_queue_empty(&ctx->sh->queue)) {//�����������û�������Ͳ���ɾ����
            return;
        }
        q = ngx_queue_last(&ctx->sh->queue);//����ȡ���е����һ����õ������õ�ʱ��
        lr = ngx_queue_data(q, ngx_http_limit_req_node_t, queue);
        if (n++ != 0) {
            ms = (ngx_msec_int_t) (now - lr->last);//����ʱ������ǰ��ʱ��
            ms = ngx_abs(ms);//���60��
            if (ms < 60000) {
                return;
            }
            excess = lr->excess - ctx->rate * ms / 1000;//����ɶ��˼
            if (excess > 0) {
                return;
            }
        }
		//�������ʱ�Ľڵ�Ӷ��У��������ɾ����Ȼ�����
        ngx_queue_remove(q);
        node = (ngx_rbtree_node_t *) ((u_char *) lr - offsetof(ngx_rbtree_node_t, color));
        ngx_rbtree_delete(&ctx->sh->rbtree, node);
	
        ngx_slab_free_locked(ctx->shpool, node);
    }
}


static ngx_int_t
ngx_http_limit_req_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{//��ʼ�������õĹ����ڴ棬����������С�ctx->sh��ctx->shpool
    ngx_http_limit_req_ctx_t  *octx = data;
    size_t                     len;
    ngx_http_limit_req_ctx_t  *ctx;

    ctx = shm_zone->data;
    if (octx) {//reload��ʱ��Ҳ�����������ʱ��data��Ϊ�ա�
        if (ngx_strcmp(ctx->var.data, octx->var.data) != 0) {
            ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                          "limit_req \"%V\" uses the \"%V\" variable while previously it used the \"%V\" variable",
                          &shm_zone->shm.name, &ctx->var, &octx->var);
            return NGX_ERROR;
        }
		//����octx�Ǳ����ڴ��з���ģ�Ҳ����old_cycle�з���ģ�������Ҫ���µ�ctx�����³�ʼ��һ��  
        // ��������ֻ�ǹ��ڱ����ڴ�����³�ʼ���������ڹ����ڴ��еĳ�ʼ�������Ͳ���Ҫ������  
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
{//����"limit_req_zone"ָ���ʱ��������
    u_char                    *p;
    size_t                     size, len;
    ngx_str_t                 *value, name, s;
    ngx_int_t                  rate, scale;//scaleΪʱ�䵥λ��ÿ�뻹��ÿ����
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
            p = (u_char *) ngx_strchr(name.data, ':');//�ҵ�name:size
            if (p) {
                *p = '\0';
                name.len = p - name.data;
                p++;
                s.len = value[i].data + value[i].len - p;
                s.data = p;//��С�ֶ�

                size = ngx_parse_size(&s);//����һ�´�С�������ֽ�����
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
            value[i].len--;//��Ҫ��������
            value[i].data++;
            ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_limit_req_ctx_t));
            if (ctx == NULL) {
                return NGX_CONF_ERROR;
            }
			//���ݱ������֣����һ������һ�����cmcf->variables.nelts ���档������set/get_handler��û�����úá�Ϊ��
            ctx->index = ngx_http_get_variable_index(cf, &value[i]);//������������֣���ȡ��������������±ꡣ
            if (ctx->index == NGX_ERROR) {
                return NGX_CONF_ERROR;
            }
            ctx->var = value[i];//��ס���������֡�
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
    ctx->rate = rate * 1000 / scale;//���ÿ���������������
	//����������֣�����һ�鹲���ڴ棬��СΪsize������¼init��ʼ����������¼ngx_http_limit_req_ctx_t�������Ľṹ��
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
    shm_zone->data = ctx;//��¼������׼����ctx�������¼��rate,index,var����Ϣ��
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
        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {//��¼���֡�
            s.len = value[i].len - 5;
            s.data = value[i].data + 5;
			//����һ���Ѿ��������ֵģ�����������һ�������ڴ�ṹ������&cf->cycle->shared_memory.part���档
            lrcf->shm_zone = ngx_shared_memory_add(cf, &s, 0, &ngx_http_limit_req_module);
            if (lrcf->shm_zone == NULL) {
                return NGX_CONF_ERROR;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "burst=", 6) == 0) {//ͻ����Ŀ��
            burst = ngx_atoi(value[i].data + 6, value[i].len - 6);
            if (burst <= 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid burst rate \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "nodelay", 7) == 0) {
            lrcf->nodelay = 1;//������nodelay��־���������ص������ֱ�ӷ���503������ԥ��
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
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);//��PREACCESS����֮ǰ����һ����λ���������handler
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_limit_req_handler;//ע���������Ϊphrase���˺�����������content phrase֮ǰ�����������з���Ƶ�����ơ�
    return NGX_OK;
}
