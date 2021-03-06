
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static char *ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_init_phases(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);
static ngx_int_t ngx_http_init_headers_in_hash(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);
static ngx_int_t ngx_http_init_phase_handlers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);

static ngx_int_t ngx_http_add_addresses(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port,
    ngx_http_listen_opt_t *lsopt);
static ngx_int_t ngx_http_add_address(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port,
    ngx_http_listen_opt_t *lsopt);
static ngx_int_t ngx_http_add_server(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_addr_t *addr);

static char *ngx_http_merge_servers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_http_module_t *module,
    ngx_uint_t ctx_index);
static char *ngx_http_merge_locations(ngx_conf_t *cf,
    ngx_queue_t *locations, void **loc_conf, ngx_http_module_t *module,
    ngx_uint_t ctx_index);
static ngx_int_t ngx_http_init_locations(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_core_loc_conf_t *pclcf);
static ngx_int_t ngx_http_init_static_location_trees(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *pclcf);
static ngx_int_t ngx_http_cmp_locations(const ngx_queue_t *one,
    const ngx_queue_t *two);
static ngx_int_t ngx_http_join_exact_locations(ngx_conf_t *cf,
    ngx_queue_t *locations);
static void ngx_http_create_locations_list(ngx_queue_t *locations,
    ngx_queue_t *q);
static ngx_http_location_tree_node_t *
    ngx_http_create_locations_tree(ngx_conf_t *cf, ngx_queue_t *locations,
    size_t prefix);

static ngx_int_t ngx_http_optimize_servers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_array_t *ports);
static ngx_int_t ngx_http_server_names(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_http_conf_addr_t *addr);
static ngx_int_t ngx_http_cmp_conf_addrs(const void *one, const void *two);
static int ngx_libc_cdecl ngx_http_cmp_dns_wildcards(const void *one,
    const void *two);

static ngx_int_t ngx_http_init_listening(ngx_conf_t *cf,
    ngx_http_conf_port_t *port);
static ngx_listening_t *ngx_http_add_listening(ngx_conf_t *cf,
    ngx_http_conf_addr_t *addr);
static ngx_int_t ngx_http_add_addrs(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr);
#if (NGX_HAVE_INET6)
static ngx_int_t ngx_http_add_addrs6(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr);
#endif

ngx_uint_t   ngx_http_max_module;


ngx_int_t  (*ngx_http_top_header_filter) (ngx_http_request_t *r);
ngx_int_t  (*ngx_http_top_body_filter) (ngx_http_request_t *r, ngx_chain_t *ch);


ngx_str_t  ngx_http_html_default_types[] = {
    ngx_string("text/html"),
    ngx_null_string
};


static ngx_command_t  ngx_http_commands[] = {

    { ngx_string("http"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_block,//处理http{}这种块
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_http_module_ctx = {
    ngx_string("http"),
    NULL,
    NULL
};


ngx_module_t  ngx_http_module = {
    NGX_MODULE_V1,
    &ngx_http_module_ctx,                  /* module context */
    ngx_http_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/*这个在ngx_conf_parse里面，会调用ngx_conf_handler,然后调用对应模块的set函数，这里ngx_http_commands设置为下面的函数
用户在碰到"http"字符串的时候，调用下面的函数，表示一个http的block要出现了，你准备一下相关数据结构.
对应的还有ngx_events_block等.
conf参数为指向cycle->conf_ctx[http_module]的地址，这个在ngx_conf_handler可以看出来。
conf = &( ( (void **) cf->ctx )[ngx_modules[i]->index] );conf代表所属模块在所有模块列表里面的配置，也就是conf_ctx[]
rv = cmd->set(cf, cmd, conf);
cf 所属的上级conf，指向cycle->conf_ctx，还没来得及修改为本http的ctx(ngx_http_conf_ctx_t)
*/
static char * ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                        *rv;
    ngx_uint_t                   mi, m, s;
    ngx_conf_t                   pcf;
    ngx_http_module_t           *module;
    ngx_http_conf_ctx_t         *ctx;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_http_core_main_conf_t   *cmcf;

    /* the main http context */
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *(ngx_http_conf_ctx_t **) conf = ctx;//修改cycle->conf_ctx[ngx_http_module]这个指针的指向，指向刚刚分配的ngx_http_conf_ctx_t
//新分配的，我的conf上下文结构啦，赋值一下，这里conf其实是指向&( ( (void **) cf->ctx )[ngx_modules[i]->index] )的。也就是cycle->conf_ctx[]数组里面，http模块，第7个模块。
    /* count the number of the http modules and set up their indices */
    ngx_http_max_module = 0;//看看这里有多少NGX_HTTP_MODULE类型的模块
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
//将这个模块的ctx_index下标设置为在NGX_HTTP_MODULE中排第几
        ngx_modules[m]->ctx_index = ngx_http_max_module++;
    }

    /* the http main_conf context, it is the same in the all http contexts */
    ctx->main_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);//每一个http模块都有一套配置。
    if (ctx->main_conf == NULL) {// 每个NGX_HTTP_MODULE模块一个
        return NGX_CONF_ERROR;
    }
    /* the http null srv_conf context, it is used to merge
     * the server{}s' srv_conf's
     */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    /* the http null loc_conf context, it is used to merge
     * the server{}s' loc_conf's
     */
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    /*
     * create the main_conf's, the null srv_conf's, and the null loc_conf's
     * of the all http modules
     */

    for (m = 0; ngx_modules[m]; m++) {//又遍历所有的模块，幸亏这是初始化
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;//只处理HTTP的模块
        module = ngx_modules[m]->ctx;
        mi = ngx_modules[m]->ctx_index;//在NGX_HTTP_MODULE模块中的序号

        if (module->create_main_conf) {//如果它设置了create_main_conf回调，调用之，让他自己去准备一下。ngx_http_module_t
            ctx->main_conf[mi] = module->create_main_conf(cf);
            if (ctx->main_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        if (module->create_srv_conf) {
            ctx->srv_conf[mi] = module->create_srv_conf(cf);
            if (ctx->srv_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        if (module->create_loc_conf) {
            ctx->loc_conf[mi] = module->create_loc_conf(cf);
            if (ctx->loc_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;//保留这个cf，待会要进入下一级
    cf->ctx = ctx;//下面的就是ngx_http_conf_ctx_t模块相关的啦，换 一下上下文，这样在后面ngx_conf_parse->ngx_conf_handler的时候，调用set的时候cf不同，conf不同。
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;
        if (module->preconfiguration) {//回调一下，注意下面的上下文变了哦，得到的将是ngx_http_conf_ctx_t结构
            if (module->preconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }
    /* parse inside the http{} block */
    cf->module_type = NGX_HTTP_MODULE;
    cf->cmd_type = NGX_HTTP_MAIN_CONF;//标注下面进行的是main的conf，这样会影响ctx的选择。
    rv = ngx_conf_parse(cf, NULL);//处理 http{} 之间的。不少东西

    if (rv != NGX_CONF_OK) {
        goto failed;
    }

    /*
     * init http{} main_conf's, merge the server{}s' srv_conf's
     * and its location{}s' loc_conf's
     */
    cmcf = ctx->main_conf[ngx_http_core_module.ctx_index];//ngx_http_core_main_conf_t 类型.ngx_http_core_module是HTTP模块的老祖
//这个是主要的了，ctx_index序号在初始化main_conf的时候设置了的，其函数在ngx_http_core_module_ctx
    cscfp = cmcf->servers.elts;//得到有多少个server{}结构
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;
        mi = ngx_modules[m]->ctx_index;
        /* init http{} main_conf's */

        if (module->init_main_conf) {//调用init_main_conf回调
            rv = module->init_main_conf(cf, ctx->main_conf[mi]);//在初始化main配置时调用（比如，把原来的默认值用nginx.conf读到的值来覆盖）
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }
		//合并配置
        rv = ngx_http_merge_servers(cf, cmcf, module, mi);
        if (rv != NGX_CONF_OK) {
            goto failed;
        }
    }
    /* create location trees */
    for (s = 0; s < cmcf->servers.nelts; s++) {//遍历每一个server，处理其loc_conf
        clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];
        if (ngx_http_init_locations(cf, cscfp[s], clcf) != NGX_OK) {
            return NGX_CONF_ERROR;//对locations进行排序，分类，以备待会建立三叉树优化查询性能
        }
        if (ngx_http_init_static_location_trees(cf, clcf) != NGX_OK) {
            return NGX_CONF_ERROR;//建立一颗三叉树，优化查询性能。
        }
    }

    if (ngx_http_init_phases(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
	//初始化请求头的处理函数哈希表。比如不同的请求头对应的哈希表
    if (ngx_http_init_headers_in_hash(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;
        if (module->postconfiguration) {
            if (module->postconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }
	//初始化变量系统里面著名的头部字段。
    if (ngx_http_variables_init_vars(cf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    /*
     * http{}'s cf->ctx was needed while the configuration merging
     * and in postconfiguration process
     */

    *cf = pcf;
	//初始化过程处理数组，将他们展开。
    if (ngx_http_init_phase_handlers(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    /* optimize the lists of ports, addresses and server names */
    if (ngx_http_optimize_servers(cf, cmcf, cmcf->ports) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
failed:
    *cf = pcf;
    return rv;
}


static ngx_int_t
ngx_http_init_phases(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_POST_READ_PHASE].handlers,  cf->pool, 1, sizeof(ngx_http_handler_pt))  != NGX_OK)  {
        return NGX_ERROR;
    }
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers,  cf->pool, 1, sizeof(ngx_http_handler_pt)) != NGX_OK) {
        return NGX_ERROR;
    }
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers,   cf->pool, 1, sizeof(ngx_http_handler_pt)) != NGX_OK)  {
        return NGX_ERROR;
    }
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers,  cf->pool, 1, sizeof(ngx_http_handler_pt))  != NGX_OK) {
        return NGX_ERROR;
    }
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers,  cf->pool, 2, sizeof(ngx_http_handler_pt))  != NGX_OK) {
        return NGX_ERROR;
    }
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers,  cf->pool, 4, sizeof(ngx_http_handler_pt)) != NGX_OK)  {
        return NGX_ERROR;
    }
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers,  cf->pool, 1, sizeof(ngx_http_handler_pt))  != NGX_OK)  {
        return NGX_ERROR;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_init_headers_in_hash(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    ngx_array_t         headers_in;
    ngx_hash_key_t     *hk;
    ngx_hash_init_t     hash;
    ngx_http_header_t  *header;

    if (ngx_array_init(&headers_in, cf->temp_pool, 32, sizeof(ngx_hash_key_t))  != NGX_OK)  {
        return NGX_ERROR;
    }
	//下面遍历一下预定义的著名头部。将他们放入headers_in遍历中。
    for (header = ngx_http_headers_in; header->name.len; header++) {
        hk = ngx_array_push(&headers_in);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = header->name;
        hk->key_hash = ngx_hash_key_lc(header->name.data, header->name.len);
        hk->value = header;
    }

    hash.hash = &cmcf->headers_in_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "headers_in_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, headers_in.elts, headers_in.nelts) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_init_phase_handlers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{//将各个阶段的注册句柄展开为数组，方便ngx_http_core_run_phases进行遍历处理
//这个函数将cmcf->phases[].handlers.nelts里面的回调函数全部展平了。
    ngx_int_t                   j;
    ngx_uint_t                  i, n;
    ngx_uint_t                  find_config_index, use_rewrite, use_access;
    ngx_http_handler_pt        *h;
    ngx_http_phase_handler_t   *ph;
    ngx_http_phase_handler_pt   checker;

    cmcf->phase_engine.server_rewrite_index = (ngx_uint_t) -1;
    cmcf->phase_engine.location_rewrite_index = (ngx_uint_t) -1;
    find_config_index = 0;
	//是否有使用rewrite以及access。
    use_rewrite = cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers.nelts ? 1 : 0;
    use_access = cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers.nelts ? 1 : 0;
	//try_files $uri $uri/ /index.php?q=$uri&$args;，加上是否配置了try_files指令
    n = use_rewrite + use_access + cmcf->try_files + 1 /* find config phase */;//这里为啥要增加呢，因为有个默认的吗�?
    for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {//对每一个间断，统计其句柄数目。开始计算handler 数组的大小  
        n += cmcf->phases[i].handlers.nelts;//这个在哪里设置的?答案在ngx_http_module_t里面的postconfiguration回调。比如ngx_http_autoindex_module模块
        //比如如果碰到access指令，就会设置postconfiguration=ngx_http_access_init，后者里面会注册handlers的，表示我关心这个阶段的处理。
    }
	//根据回到的句柄数目，申请这么多个数组项。
    ph = ngx_pcalloc(cf->pool,n * sizeof(ngx_http_phase_handler_t) + sizeof(void *));
    if (ph == NULL) {
        return NGX_ERROR;
    }
	//下面将每个不同的处理阶段的回调句柄展平，方便ngx_http_core_run_phases进行轮询遍历。
    cmcf->phase_engine.handlers = ph;//所有的handler都在这个数组里面.
    n = 0;
    for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {//对于每一个处理阶段，将其展平
        h = cmcf->phases[i].handlers.elts;//取得当前一个过程的句柄数组，这个是各个模块注册的。

        switch (i) {//对不同的阶段进行不同的处理。
        case NGX_HTTP_SERVER_REWRITE_PHASE:
            if (cmcf->phase_engine.server_rewrite_index == (ngx_uint_t) -1) {
                cmcf->phase_engine.server_rewrite_index = n;//标记一下，NGX_HTTP_SERVER_REWRITE_PHASE这个阶段的下标，或者说起始位置
            }
            checker = ngx_http_core_rewrite_phase;//使用这个统一的checker,其里面会调用对应的handler
            break;//跳到后面进行处理，会拷贝相关结构

        case NGX_HTTP_FIND_CONFIG_PHASE:
            find_config_index = n;//FIND_CONFIG_PHASE么有很多句柄回调需要处理，就一个，所以就直接continue了。
            //其next成员为0，所以这里如果找到了配置，会重新从第一部开始的。
            ph->checker = ngx_http_core_find_config_phase;//对于配置查找，只需要设置checker就行，没有handler需要设置。
            n++;//计数
            ph++;//跳到下一个
            continue;//不用进行handlers.nelts的拷贝设置了，只有一个的。

        case NGX_HTTP_REWRITE_PHASE:
            if (cmcf->phase_engine.location_rewrite_index == (ngx_uint_t) -1) {
                cmcf->phase_engine.location_rewrite_index = n;
            }
            checker = ngx_http_core_rewrite_phase;
            break;

        case NGX_HTTP_POST_REWRITE_PHASE:
            if (use_rewrite) {//就一个，直接continue
                ph->checker = ngx_http_core_post_rewrite_phase;
                ph->next = find_config_index;//链接起来，修改了rewrite后，需要再进行配置查找,通过这个方式进行循环会跳。
                n++;
                ph++;
            }
            continue;//不用进行handlers.nelts的拷贝设置了，只有一个的。

        case NGX_HTTP_ACCESS_PHASE:
            checker = ngx_http_core_access_phase;
            n++;
            break;

        case NGX_HTTP_POST_ACCESS_PHASE:
            if (use_access) {
                ph->checker = ngx_http_core_post_access_phase;
                ph->next = n;//访问权限控制后，下一个就是顺序的下一个了。因为它也只有一个的。
                ph++;
            }
            continue;//不用进行handlers.nelts的拷贝设置了，只有一个的。

        case NGX_HTTP_TRY_FILES_PHASE:
            if (cmcf->try_files) {
                ph->checker = ngx_http_core_try_files_phase;
                n++;
                ph++;
            }
            continue;//不用进行handlers.nelts的拷贝设置了，只有一个的。

        case NGX_HTTP_CONTENT_PHASE://主要的内容处理都在这里了。
            checker = ngx_http_core_content_phase;
            break;

        default:
            checker = ngx_http_core_generic_phase;
        }

        n += cmcf->phases[i].handlers.nelts;
//对于可能有多个句柄注册的阶段，需要一个个拷贝。他们的checker相同，但是handler不同。在他们注册的时候设置的，比如ngx_http_access_init
        for (j = cmcf->phases[i].handlers.nelts - 1; j >=0; j--) {//从后面往前面拷贝。为啥倒序?
            ph->checker = checker;//checker相同
            ph->handler = h[j];//handler不同。如果我们需要进行模块开发，则需要注册对应的阶段的句柄。
            ph->next = n;//下一个是第几个，标记其下标。其实代表的是下一个处理过程类型是第几个
            ph++;//循环到下一个同类型的模块的回调
        }
    }

    return NGX_OK;
}


static char *
ngx_http_merge_servers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,  ngx_http_module_t *module, ngx_uint_t ctx_index)
{
    char                        *rv;
    ngx_uint_t                   s;
    ngx_http_conf_ctx_t         *ctx, saved;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;

    cscfp = cmcf->servers.elts;
    ctx = (ngx_http_conf_ctx_t *) cf->ctx;
    saved = *ctx;
    rv = NGX_CONF_OK;

    for (s = 0; s < cmcf->servers.nelts; s++) {//对每一个servers，分别合并其svr,loc,等配置
        /* merge the server{}s' srv_conf's */
        ctx->srv_conf = cscfp[s]->ctx->srv_conf;
        if (module->merge_srv_conf) {
            rv = module->merge_srv_conf(cf, saved.srv_conf[ctx_index], cscfp[s]->ctx->srv_conf[ctx_index]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }
        if (module->merge_loc_conf) {
            /* merge the server{}'s loc_conf */
            ctx->loc_conf = cscfp[s]->ctx->loc_conf;
            rv = module->merge_loc_conf(cf, saved.loc_conf[ctx_index],   cscfp[s]->ctx->loc_conf[ctx_index]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
            /* merge the locations{}' loc_conf's */
            clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];
            rv = ngx_http_merge_locations(cf, clcf->locations, cscfp[s]->ctx->loc_conf, module, ctx_index);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }
    }
failed:
    *ctx = saved;
    return rv;
}


static char *
ngx_http_merge_locations(ngx_conf_t *cf, ngx_queue_t *locations,
    void **loc_conf, ngx_http_module_t *module, ngx_uint_t ctx_index)
{
    char                       *rv;
    ngx_queue_t                *q;
    ngx_http_conf_ctx_t        *ctx, saved;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_location_queue_t  *lq;

    if (locations == NULL) {
        return NGX_CONF_OK;
    }

    ctx = (ngx_http_conf_ctx_t *) cf->ctx;
    saved = *ctx;

    for (q = ngx_queue_head(locations); q != ngx_queue_sentinel(locations); q = ngx_queue_next(q)) {
        lq = (ngx_http_location_queue_t *) q;

        clcf = lq->exact ? lq->exact : lq->inclusive;
        ctx->loc_conf = clcf->loc_conf;
        rv = module->merge_loc_conf(cf, loc_conf[ctx_index],  clcf->loc_conf[ctx_index]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
        rv = ngx_http_merge_locations(cf, clcf->locations, clcf->loc_conf, module, ctx_index);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    *ctx = saved;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_init_locations(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf, ngx_http_core_loc_conf_t *pclcf)
{//解析完http{}后，对每一个server循环调用这里初始化locations结构数据。这里可以递归的不断层层调用。
//这里会将locations排序，然后分段保存到不同的地方。排序后的链接locations将待会用来简历locations树。
    ngx_uint_t                   n;
    ngx_queue_t                 *q, *locations, *named, tail;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_location_queue_t   *lq;
    ngx_http_core_loc_conf_t   **clcfp;
#if (NGX_PCRE)
    ngx_uint_t                   r;
    ngx_queue_t                 *regex;
#endif

    locations = pclcf->locations;//一个队列，存有所有的location指令信息
    if (locations == NULL) {
        return NGX_OK;
    }
	//最后的顺序为: <简单字符串匹配，递归节点在后面> <正则匹配,内部不排序> <@命名的,内部字符串排序>  <if无名的，内部无序>
    ngx_queue_sort(locations, ngx_http_cmp_locations);//对location进行排序,从小到大。
    named = NULL;
    n = 0;
#if (NGX_PCRE)
    regex = NULL;
    r = 0;
#endif

    for (q = ngx_queue_head(locations); q != ngx_queue_sentinel(locations);  q = ngx_queue_next(q)) {
        lq = (ngx_http_location_queue_t *) q;
        clcf = lq->exact ? lq->exact : lq->inclusive;//得到配置。
        if (ngx_http_init_locations(cf, NULL, clcf) != NGX_OK) {//递归调用当前locatioin的配置，如果里面嵌套了location，则递归不会立即退出的。
            return NGX_ERROR;
        }
#if (NGX_PCRE)
        if (clcf->regex) {
            r++;//正则表达式计数+1
            if (regex == NULL) {
                regex = q;//记录这个，是正则。
            }
            continue;//继续遍历
        }
#endif
        if (clcf->named) {//直接名字的
            n++;//计数
            if (named == NULL) {
                named = q;//记录这个直接名字的。
            }
            continue;
        }
        if (clcf->noname) {
            break;//如果到这，说明遇到了无名的location，由于先前是排序的，因此后面肯定都是无名的location.可以跳出了。
        }
    }
    if (q != ngx_queue_sentinel(locations)) {//不等于头部,说明碰到了无名的节点。q后面都是无名的。
        ngx_queue_split(locations, q, &tail);//将后面无名的，和有名的区分开来，分成locations,tail 2条队列。这些节点是不是丢了?对的
        //ngx_http_rewrite_if里面会对if指令做特殊的处理。所以这里丢了没关系的。
    }
	/* 如果有正则匹配location，将它们保存在所属server的http core模块的loc配置的regex_locations 数组中， 
	这里和named location保存位置不同的原因是由于named location只能存在server里面，而regex location可以作为nested location */	

    if (named) {//如果有命名节点。后面会有一堆的。
        clcfp = ngx_palloc(cf->pool, (n + 1) * sizeof(ngx_http_core_loc_conf_t **));
        if (clcfp == NULL) {
            return NGX_ERROR;
        }
        cscf->named_locations = clcfp;//如果有named location，将它们保存在所属server的named_locations数组中
        for (q = named;  q != ngx_queue_sentinel(locations); q = ngx_queue_next(q)) {//此时的locations不包括if无名位置
            lq = (ngx_http_location_queue_t *) q;
            *(clcfp++) = lq->exact;//拷贝命名节点的配置。
        }
        *clcfp = NULL;
        ngx_queue_split(locations, named, &tail);//cong第一个named分割为2部分。如果上面的分割起作用，那么这次是什么意思?上面那段是不是没了?
    }
#if (NGX_PCRE)
    if (regex) {//有正则。后面肯定全部是正则，因为在上面切掉了。
        clcfp = ngx_palloc(cf->pool, (r + 1) * sizeof(ngx_http_core_loc_conf_t **));
        if (clcfp == NULL) {
            return NGX_ERROR;
        }
        pclcf->regex_locations = clcfp;
        for (q = regex;  q != ngx_queue_sentinel(locations); q = ngx_queue_next(q)) {
            lq = (ngx_http_location_queue_t *) q;//将正则的location拷贝到regex_locations
            *(clcfp++) = lq->exact;
        }
        *clcfp = NULL;
        ngx_queue_split(locations, regex, &tail);//将正则的节点切除。前面就全部是静态的location了
    }
#endif
    return NGX_OK;
}


static ngx_int_t
ngx_http_init_static_location_trees(ngx_conf_t *cf, ngx_http_core_loc_conf_t *pclcf)
{//上层调用者循环每个server，调用这里建立一颗locations包含匹配的树，以优化查询速度。
//下面分2步，第一步为梳理locations链接，建立一颗前缀树
    ngx_queue_t                *q, *locations;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_location_queue_t  *lq;

    locations = pclcf->locations;//这里面已经在ngx_http_init_locations里整理成为了静态location的双链表
    if (locations == NULL) {
        return NGX_OK;
    }
    if (ngx_queue_empty(locations)) {//这里面肯定都是静态的节点了
        return NGX_OK;
    }
    for (q = ngx_queue_head(locations);  q != ngx_queue_sentinel(locations);  q = ngx_queue_next(q))  {
        lq = (ngx_http_location_queue_t *) q;
        clcf = lq->exact ? lq->exact : lq->inclusive;//得到保存loc的配置结构。
        if (ngx_http_init_static_location_trees(cf, clcf) != NGX_OK) {//递归嵌套location
            return NGX_ERROR;//递归创建下一个location的静态位置树
        }
    }
	//合并相同的static位置配置
    if (ngx_http_join_exact_locations(cf, locations) != NGX_OK) {
        return NGX_ERROR;
    }
    ngx_http_create_locations_list(locations, ngx_queue_head(locations));//排序。整理相同前缀的节点到list成员上面

    pclcf->static_locations = ngx_http_create_locations_tree(cf, locations, 0);//对所有的非精确匹配locations，建立一颗三叉树，加速查找
    if (pclcf->static_locations == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_add_location(ngx_conf_t *cf, ngx_queue_t **locations,  ngx_http_core_loc_conf_t *clcf)
{//将clcf所指的位置配置链接到locations的后面，插入后面。也就是增加一个location
//clcf为location的ctx->loc_conf数组。
    ngx_http_location_queue_t  *lq;
    if (*locations == NULL) {//如果还没有初始化，就创建locations数组
        *locations = ngx_palloc(cf->temp_pool,  sizeof(ngx_http_location_queue_t));
        if (*locations == NULL) {
            return NGX_ERROR;
        }
        ngx_queue_init(*locations);
    }
    lq = ngx_palloc(cf->temp_pool, sizeof(ngx_http_location_queue_t));
    if (lq == NULL) {
        return NGX_ERROR;
    }
    if (clcf->exact_match //如果是=等于绝对匹配
#if (NGX_PCRE)
        || clcf->regex //如果是~ 正则匹配
#endif
        || clcf->named //指定了一个@开头的命名location.
        || clcf->noname)//这是在ngx_http_rewrite_if碰到if语句才有的东西。
    {//所有这些都是确确实实滴结构。
        lq->exact = clcf;//设置为上面创建的ngx_http_core_loc_conf_t成员
        lq->inclusive = NULL;
    } else {//这里面是location  /xyz {  这种类型的。既不是绝对匹配，也不是正则。所谓非exact，而是包含匹配
        lq->exact = NULL;
        lq->inclusive = clcf;//可能有包含匹配，需要建立树形结构。
    }
	//准备新队列节点的数据
    lq->name = &clcf->name;
    lq->file_name = cf->conf_file->file.name.data;//配置文件名。
    lq->line = cf->conf_file->line;
    ngx_queue_init(&lq->list);//初始化这个新的节点的前后指针
    ngx_queue_insert_tail(*locations, &lq->queue);//链入locations的后面，是个双链表
    return NGX_OK;
}


static ngx_int_t
ngx_http_cmp_locations(const ngx_queue_t *one, const ngx_queue_t *two)
{//从小到大排序。最后的顺序为: <简单字符串匹配，递归节点在后面> <正则匹配,内部不排序> <@命名的,内部字符串排序>  <if无名的，内部无序>
    ngx_int_t                   rc;
    ngx_http_core_loc_conf_t   *first, *second;
    ngx_http_location_queue_t  *lq1, *lq2;

    lq1 = (ngx_http_location_queue_t *) one;
    lq2 = (ngx_http_location_queue_t *) two;
    first = lq1->exact ? lq1->exact : lq1->inclusive;//得到其配置。
    second = lq2->exact ? lq2->exact : lq2->inclusive;
    if (first->noname && !second->noname) { /* shift no named locations to the end */
        return 1;//有名的大于无名的。比如if ***的，应该放到后面去。
    }
    if (!first->noname && second->noname) { /* shift no named locations to the end */
        return -1;
    }
    if (first->noname || second->noname) { /* do not sort no named locations */
        return 0;
    }
    if (first->named && !second->named) { /* shift named locations to the end */
        return 1;//用@开头的命名location，也放到后面去。
    }
    if (!first->named && second->named) {
        /* shift named locations to the end */
        return -1;
    }
    if (first->named && second->named) {
        return ngx_strcmp(first->name.data, second->name.data);
    }
#if (NGX_PCRE)
    if (first->regex && !second->regex) {  /* shift the regex matches to the end */
        return 1;
    }
    if (!first->regex && second->regex) { /* shift the regex matches to the end */
        return -1;
    }
    if (first->regex || second->regex) { /* do not sort the regex matches */
        return 0;//2个都是正则。没法排序的
    }
#endif
    rc = ngx_strcmp(first->name.data, second->name.data);
    if (rc == 0 && !first->exact_match && second->exact_match) { /* an exact match must be before the same inclusive one */
        return 1;
    }
    return rc;
}


static ngx_int_t
ngx_http_join_exact_locations(ngx_conf_t *cf, ngx_queue_t *locations)
{//将相邻的配置给河滨一下。
    ngx_queue_t                *q, *x;
    ngx_http_location_queue_t  *lq, *lx;

    q = ngx_queue_head(locations);
    while (q != ngx_queue_last(locations)) {//已经是排过序的，所以只要判断相邻的就行了。
        x = ngx_queue_next(q);
        lq = (ngx_http_location_queue_t *) q;
        lx = (ngx_http_location_queue_t *) x;
        if (ngx_strcmp(lq->name->data, lx->name->data) == 0) {//如果名字相同。
            if ((lq->exact && lx->exact) || (lq->inclusive && lx->inclusive)) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "duplicate location \"%V\" in %s:%ui", lx->name, lx->file_name, lx->line);
                return NGX_ERROR;
            }
            lq->inclusive = lx->inclusive;
            ngx_queue_remove(x);
            continue;
        }
        q = ngx_queue_next(q);
    }
    return NGX_OK;
}


static void
ngx_http_create_locations_list(ngx_queue_t *locations, ngx_queue_t *q)
{//递归函数，简历一颗前缀树了基本。由list指向左边的前缀相同的节点。
    u_char                     *name;
    size_t                      len;
    ngx_queue_t                *x, tail;
    ngx_http_location_queue_t  *lq, *lx;

    if (q == ngx_queue_last(locations)) {//从最后一个开始。
        return;
    }
    lq = (ngx_http_location_queue_t *) q;
    if (lq->inclusive == NULL) {//如果这个节点不是包含的关系，比如是exact绝对的匹配，那就不用建树了，因为是绝对匹配。
        ngx_http_create_locations_list(locations, ngx_queue_next(q));
        return;
    }
	//如下，打个比方，locations列表如下: a, ab, ac ad b bc 
    len = lq->name->len;//此时lq指向节点
    name = lq->name->data;
    for (x = ngx_queue_next(q);  x != ngx_queue_sentinel(locations); x = ngx_queue_next(x)) {
        lx = (ngx_http_location_queue_t *) x;
        if (len > lx->name->len  || (ngx_strncmp(name, lx->name->data, len) != 0)) {
            break;//找一个长度小于第一个的，或者前缀不想等的。也就是要找出b节点。因为前面的a,ab,ac,ad都是前缀相同的。可以建树
        }
    }
    q = ngx_queue_next(q);//
    if (q == x) {//就一个嵌套节点，没用。
        ngx_http_create_locations_list(locations, x);
        return;
    }
	//此时q指向ab点
    ngx_queue_split(locations, q, &tail);//将a于ab,ac,ad,b,bc分开为2断。
    ngx_queue_add(&lq->list, &tail);//将ab,ac,ad,b,bc接到a节点的list成员上面
    if (x == ngx_queue_sentinel(locations)) {//x为b节点，因为此处b节点不是最后一个节点，也没有结束。下面不成立
        ngx_http_create_locations_list(&lq->list, ngx_queue_head(&lq->list));
        return;
    }
    ngx_queue_split(&lq->list, x, &tail);//将刚刚接下去的a,ab,ac,ad,b,bc链表从b处截断
    ngx_queue_add(locations, &tail);//将刚截断的b,bc放到locations的后面，也就是说，刚才将ab,ac,ad这条有前缀相同的链放入了a节点的list成员下面。
    ngx_http_create_locations_list(&lq->list, ngx_queue_head(&lq->list));//这里处理ab,ac,ad的list链接。
    ngx_http_create_locations_list(locations, x);//继续从刚刚的b点开始处理。
}


/*
 * to keep cache locality for left leaf nodes, allocate nodes in following
 * order: node, left subtree, right subtree, inclusive subtree
 */

static ngx_http_location_tree_node_t *
ngx_http_create_locations_tree(ngx_conf_t *cf, ngx_queue_t *locations, size_t prefix)
{//根据ngx_http_create_locations_list整理的带有list的排序的简单树结构，整理出一颗三叉树.
//任何一个节点，不算中间节点，2边的节点数目相等。也就是每次去中间点为根。将有相同前缀的list列表放入tree子树中。
    size_t                          len;
    ngx_queue_t                    *q, tail;
    ngx_http_location_queue_t      *lq;
    ngx_http_location_tree_node_t  *node;

    q = ngx_queue_middle(locations);//用双指针，2步移动的方式找到中间的节点q

    lq = (ngx_http_location_queue_t *) q;
    len = lq->name->len - prefix;//这个节点前面已经有prefix长的字符是已经在父节点处理过了的。不用再次比较

    node = ngx_palloc(cf->pool, offsetof(ngx_http_location_tree_node_t, name) + len);
    if (node == NULL) {
        return NULL;
    }
    node->left = NULL;
    node->right = NULL;
    node->tree = NULL;
    node->exact = lq->exact;
    node->inclusive = lq->inclusive;
    node->auto_redirect = (u_char) ((lq->exact && lq->exact->auto_redirect) || (lq->inclusive && lq->inclusive->auto_redirect));
    node->len = (u_char) len;
    ngx_memcpy(node->name, &lq->name->data[prefix], len);//这里只需要拷贝后面不相同的地方就行了

    ngx_queue_split(locations, q, &tail);//将刚才的节点后面的节点全部截断。q和其后的节点都在tail上面。
    if (ngx_queue_empty(locations)) {
        /*
         * ngx_queue_split() insures that if left part is empty,
         * then right one is empty too
         */
        goto inclusive;
    }
    node->left = ngx_http_create_locations_tree(cf, locations, prefix);//递归处理左边的部分。
    if (node->left == NULL) {
        return NULL;
    }

    ngx_queue_remove(q);//将这个节点删除。
    if (ngx_queue_empty(&tail)) {
        goto inclusive;
    }

    node->right = ngx_http_create_locations_tree(cf, &tail, prefix);//递归处理右边的节点列表
    if (node->right == NULL) {
        return NULL;
    }

inclusive:
    if (ngx_queue_empty(&lq->list)) {//如果刚刚有节点放入了我的list成员中，说明我和下面的list列表有相同前缀，此时应该进入了。
        return node;
    }
	//这是重点，有相同前缀的地方，要进入。上面的左右节点都太平常了，这里真正处理有前缀相同的包含匹配情况。
    node->tree = ngx_http_create_locations_tree(cf, &lq->list, prefix + len);//返回值放入tree，也就是:left,right,代表字符串排序在前面的。tree才是前缀。
    if (node->tree == NULL) {
        return NULL;
    }

    return node;
}


ngx_int_t
ngx_http_add_listen(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf, ngx_http_listen_opt_t *lsopt)
{//将这个默认的监听端口设置到cscf里面，以及http的main_conf的ports数组里面
//当解析到一个监听端口的时候，准备一下后会调用这里。lsopt参数记录了端口的信息。
    in_port_t                   p;
    ngx_uint_t                  i;
    struct sockaddr            *sa;
    struct sockaddr_in         *sin;
    ngx_http_conf_port_t       *port;
    ngx_http_core_main_conf_t  *cmcf;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6        *sin6;
#endif
//由于在解析到server{}的时候，cf->ctx已经被设置为server的ctx，所以这里用的是server的ctx获取的main_conf
//而正好server的main_conf其实就是http的main_conf，所以下面得到的是http的main_conf。
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    if (cmcf->ports == NULL) {
		//如果整个http{}块里面当前还是第一次解析到一个监听端口，或者压根没有设置任何监听端口(有默认的).就初始化http的ports
        cmcf->ports = ngx_array_create(cf->temp_pool, 2,  sizeof(ngx_http_conf_port_t));
        if (cmcf->ports == NULL) {
            return NGX_ERROR;
        }
    }
    sa = &lsopt->u.sockaddr;
    switch (sa->sa_family) {
#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = &lsopt->u.sockaddr_in6;
        p = sin6->sin6_port;
        break;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
        p = 0;
        break;
#endif
    default: /* AF_INET */
        sin = &lsopt->u.sockaddr_in;
        p = sin->sin_port;
        break;
    }
    port = cmcf->ports->elts;
    for (i = 0; i < cmcf->ports->nelts; i++) {
        if (p != port[i].port || sa->sa_family != port[i].family) {
            continue;//协议族+端口号唯一决定一个端口。
        }
        /* a port is already in the port list */
		//如果这个端口已经存在，那么有必要处理一下不同的地址。比如listen 127.0.0.1:80; 80; 127.0.0.1; *:80 ; unix:path
        return ngx_http_add_addresses(cf, cscf, &port[i], lsopt);//如果端口已经存在，这里为啥还要调动一次ngx_http_add_addresses
    }
    /* add a port to the port list */
    port = ngx_array_push(cmcf->ports);//如注释，如果在HTTP的main_conf里面没有找到之前设置过的端口，那么这里需要增加。
    if (port == NULL) {
        return NGX_ERROR;
    }
    port->family = sa->sa_family;
    port->port = p;//转为网络序后的端口列表。ntohs()可以解码
    port->addrs.elts = NULL;//新加的端口，那么这个端口对应的server列表应该为空。里面会初始化的。
    return ngx_http_add_address(cf, cscf, port, lsopt);
}


static ngx_int_t
ngx_http_add_addresses(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port, ngx_http_listen_opt_t *lsopt)
{//port参数为http的main_conf里面的ports数组一项。该监听端口已经存在了。
//这里多做了一部分工作:默认地址检查，比如如果已经存在了这个地址，就需要检查是否重复设置了default_server
    u_char                *p;
    size_t                 len, off;
    ngx_uint_t             i, default_server;
    struct sockaddr       *sa;
    ngx_http_conf_addr_t  *addr;
#if (NGX_HAVE_UNIX_DOMAIN)
    struct sockaddr_un    *saun;
#endif
#if (NGX_HTTP_SSL)
    ngx_uint_t             ssl;
#endif
    /*
     * we can not compare whole sockaddr struct's as kernel
     * may fill some fields in inherited sockaddr struct's
     */
    sa = &lsopt->u.sockaddr;
    switch (sa->sa_family) {
#if (NGX_HAVE_INET6)
    case AF_INET6:
        off = offsetof(struct sockaddr_in6, sin6_addr);//如果是IPV6，则比较其ipv6的地址结构。
        len = 16;
        break;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
        off = offsetof(struct sockaddr_un, sun_path);//如果使用的是UNIX域，则比较域的路径名。
        len = sizeof(saun->sun_path);//字符串，最长104个字节。
        break;
#endif
    default: /* AF_INET */
        off = offsetof(struct sockaddr_in, sin_addr);//需要32位的ipv4地址
        len = 4;
        break;
    }
    p = lsopt->u.sockaddr_data + off;
    addr = port->addrs.elts;//下面扫描这个端口对应的地址列表，如果已经存在，就只需要修改一下了。否则需要新加
    for (i = 0; i < port->addrs.nelts; i++) {//为了区分同一个端口，不同的地址:listen 127.0.0.1:80; 80; 127.0.0.1; *:80 ; unix:path
        if (ngx_memcmp(p, addr[i].opt.u.sockaddr_data + off, len) != 0) {
            continue;//这里的意思是，因为端口肯定相等，所以这里比较ip地址，或者域是不是相等，是否存在。
        }
        /* the address is already in the address list */
        if (ngx_http_add_server(cf, cscf, &addr[i]) != NGX_OK) {
            return NGX_ERROR;//这里增加一下本唯一监听地址所服务的虚拟主机。
        }
        /* preserve default_server bit during listen options overwriting */
        default_server = addr[i].opt.default_server;//保存十分该监听端口是默认的server
#if (NGX_HTTP_SSL)
        ssl = lsopt->ssl || addr[i].opt.ssl;
#endif
        if (lsopt->set) {
            if (addr[i].opt.set) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "a duplicate listen options for %s", addr[i].opt.addr);
                return NGX_ERROR;
            }
            addr[i].opt = *lsopt;//记录这个地址结构的端口信息。
        }
        /* check the duplicate "default" server for this address:port */
        if (lsopt->default_server) {//配置是改端口是默认的。
            if (default_server) {//原本这个端口已经被设置过默认了。所以，重复了。
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "a duplicate default server for %s", addr[i].opt.addr);
                return NGX_ERROR;
            }
            default_server = 1;
            addr[i].default_server = cscf;//没有设置过默认，这里标记一下该server的srv_conf配置。
        }
        addr[i].opt.default_server = default_server;//标记被设置过了默认虚拟
#if (NGX_HTTP_SSL)
        addr[i].opt.ssl = ssl;
#endif
        return NGX_OK;
    }
    /* add the address to the addresses list that bound to this port */
    return ngx_http_add_address(cf, cscf, port, lsopt);
}

/*
 * add the server address, the server names and the server core module
 * configurations to the port list
 */
static ngx_int_t
ngx_http_add_address(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port, ngx_http_listen_opt_t *lsopt)
{//找到一个端口，其有新的地址需要加入到addrss里面。
    ngx_http_conf_addr_t  *addr;
    if (port->addrs.elts == NULL) {
        if (ngx_array_init(&port->addrs, cf->temp_pool, 4, sizeof(ngx_http_conf_addr_t))  != NGX_OK) {
            return NGX_ERROR;
        }
    }
    addr = ngx_array_push(&port->addrs);
    if (addr == NULL) {
        return NGX_ERROR;
    }
    addr->opt = *lsopt;//记录这个listen **;指令的配置信息。纯配置。
    addr->hash.buckets = NULL;
    addr->hash.size = 0;
    addr->wc_head = NULL;
    addr->wc_tail = NULL;
#if (NGX_PCRE)
    addr->nregex = 0;
    addr->regex = NULL;
#endif//第一个碰到的就是其默认的server.注意，这个默认跟ngx_http_add_addresses里面判断重复默认虚拟主机不一样。
    addr->default_server = cscf;//ngx_http_add_addresses里面的判断默认是指是否有配置项明确指出了default。此处只是指向一下第一个虚拟主机。
    addr->servers.elts = NULL;//由于这个唯一的监听地址结构还是第一次出现，因此不知道其服务于哪些servers。不过下面就知道一个了。
    return ngx_http_add_server(cf, cscf, addr);
}


/* add the server core module configuration to the address:port */
static ngx_int_t
ngx_http_add_server(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,  ngx_http_conf_addr_t *addr)
{//先判断一下这个listen **;是否指向重复的server{}，如果没有。就在这个唯一监听地址的addr结构的servers里面新加一项。指向该server{}的srv_conf
    ngx_uint_t                  i;
    ngx_http_core_srv_conf_t  **server;
	
    if (addr->servers.elts == NULL) {
        if (ngx_array_init(&addr->servers, cf->temp_pool, 4,  sizeof(ngx_http_core_srv_conf_t *)) != NGX_OK) {
            return NGX_ERROR;
        }
    } else {//本addr已经有虚拟主机注册了，下面需要搜索一遍，别重复了。
        server = addr->servers.elts;
        for (i = 0; i < addr->servers.nelts; i++) {
            if (server[i] == cscf) {//这个够悲剧，这条listen **;指令竟然指向了一个相同的虚拟主机。也就是相同的server{}结构/cscf.
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "a duplicate listen %s", addr->opt.addr);
                return NGX_ERROR;
            }
        }
    }

    server = ngx_array_push(&addr->servers);
    if (server == NULL) {
        return NGX_ERROR;
    }
    *server = cscf;
    return NGX_OK;
}


static ngx_int_t
ngx_http_optimize_servers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf, ngx_array_t *ports)
{//NGX完全加载完HTTP配置之后，再调用这里，进行额外的工作，比如listening端口设置
    ngx_uint_t             p, a;
    ngx_http_conf_port_t  *port;
    ngx_http_conf_addr_t  *addr;

    if (ports == NULL) {
        return NGX_OK;
    }
    port = ports->elts;//对每一个端口，
    for (p = 0; p < ports->nelts; p++) {
        ngx_sort(port[p].addrs.elts, (size_t) port[p].addrs.nelts,  sizeof(ngx_http_conf_addr_t), ngx_http_cmp_conf_addrs);
        /*
         * check whether all name-based servers have the same
         * configuraiton as a default server for given address:port
         */
        addr = port[p].addrs.elts;//扫描所有端口
        for (a = 0; a < port[p].addrs.nelts; a++) {
            if (addr[a].servers.nelts > 1
#if (NGX_PCRE)
                || addr[a].default_server->captures
#endif
               ) {
                if (ngx_http_server_names(cf, cmcf, &addr[a]) != NGX_OK) {//看看该端口锁对应的配置
                    return NGX_ERROR;
                }
            }
        }

        if (ngx_http_init_listening(cf, &port[p]) != NGX_OK) {
            return NGX_ERROR;//初始化这个端口的相关配置，比如初始化handler函数等
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_server_names(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf, ngx_http_conf_addr_t *addr)
{
    ngx_int_t                   rc;
    ngx_uint_t                  n, s;
    ngx_hash_init_t             hash;
    ngx_hash_keys_arrays_t      ha;
    ngx_http_server_name_t     *name;
    ngx_http_core_srv_conf_t  **cscfp;
#if (NGX_PCRE)
    ngx_uint_t                  regex, i;

    regex = 0;
#endif

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
    ha.temp_pool = ngx_create_pool(16384, cf->log);
    if (ha.temp_pool == NULL) {
        return NGX_ERROR;
    }
    ha.pool = cf->pool;
    if (ngx_hash_keys_array_init(&ha, NGX_HASH_LARGE) != NGX_OK) {
        goto failed;
    }
    cscfp = addr->servers.elts;//得到这个
    for (s = 0; s < addr->servers.nelts; s++) {//对这个端口对应的所有servers
    //这个循环的意思是判断一下是否有端口，在2个不同的
        name = cscfp[s]->server_names.elts;
        for (n = 0; n < cscfp[s]->server_names.nelts; n++) {//对每个server里面的不同虚拟主机名字

#if (NGX_PCRE)
            if (name[n].regex) {
                regex++;
                continue;
            }
#endif
			//加进这个hash里面，如果已经存在，那说明重复了
            rc = ngx_hash_add_key(&ha, &name[n].name, name[n].server,  NGX_HASH_WILDCARD_KEY);
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
            if (rc == NGX_DECLINED) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "invalid server name or wildcard \"%V\" on %s",  &name[n].name, addr->opt.addr);
                return NGX_ERROR;
            }

            if (rc == NGX_BUSY) {
            ngx_log_error(NGX_LOG_WARN, cf->log, 0,  "conflicting server name \"%V\" on %s, ignored", &name[n].name, addr->opt.addr);
            }
        }
    }

    hash.key = ngx_hash_key_lc;
    hash.max_size = cmcf->server_names_hash_max_size;
    hash.bucket_size = cmcf->server_names_hash_bucket_size;
    hash.name = "server_names_hash";
    hash.pool = cf->pool;

    if (ha.keys.nelts) {
        hash.hash = &addr->hash;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
            goto failed;
        }
    }

    if (ha.dns_wc_head.nelts) {

        ngx_qsort(ha.dns_wc_head.elts, (size_t) ha.dns_wc_head.nelts,
                  sizeof(ngx_hash_key_t), ngx_http_cmp_dns_wildcards);

        hash.hash = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_head.elts,
                                   ha.dns_wc_head.nelts)
            != NGX_OK)
        {
            goto failed;
        }

        addr->wc_head = (ngx_hash_wildcard_t *) hash.hash;
    }

    if (ha.dns_wc_tail.nelts) {

        ngx_qsort(ha.dns_wc_tail.elts, (size_t) ha.dns_wc_tail.nelts,
                  sizeof(ngx_hash_key_t), ngx_http_cmp_dns_wildcards);

        hash.hash = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_tail.elts,
                                   ha.dns_wc_tail.nelts)
            != NGX_OK)
        {
            goto failed;
        }

        addr->wc_tail = (ngx_hash_wildcard_t *) hash.hash;
    }

    ngx_destroy_pool(ha.temp_pool);

#if (NGX_PCRE)

    if (regex == 0) {
        return NGX_OK;
    }

    addr->nregex = regex;
    addr->regex = ngx_palloc(cf->pool, regex * sizeof(ngx_http_server_name_t));
    if (addr->regex == NULL) {
        return NGX_ERROR;
    }

    i = 0;

    for (s = 0; s < addr->servers.nelts; s++) {

        name = cscfp[s]->server_names.elts;

        for (n = 0; n < cscfp[s]->server_names.nelts; n++) {
            if (name[n].regex) {
                addr->regex[i++] = name[n];
            }
        }
    }

#endif

    return NGX_OK;

failed:

    ngx_destroy_pool(ha.temp_pool);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_cmp_conf_addrs(const void *one, const void *two)
{
    ngx_http_conf_addr_t  *first, *second;

    first = (ngx_http_conf_addr_t *) one;
    second = (ngx_http_conf_addr_t *) two;
    if (first->opt.wildcard) {
        /* a wildcard address must be the last resort, shift it to the end */
        return 1;
    }
    if (first->opt.bind && !second->opt.bind) {
        /* shift explicit bind()ed addresses to the start */
        return -1;
    }
    if (!first->opt.bind && second->opt.bind) {
        /* shift explicit bind()ed addresses to the start */
        return 1;
    }
    /* do not sort by default */

    return 0;
}


static int ngx_libc_cdecl
ngx_http_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


static ngx_int_t
ngx_http_init_listening(ngx_conf_t *cf, ngx_http_conf_port_t *port)
{//设置这个listening的回调handler，然后设置询主机字段到ngx_http_port_t
    ngx_uint_t                 i, last, bind_wildcard;
    ngx_listening_t           *ls;
    ngx_http_port_t           *hport;
    ngx_http_conf_addr_t      *addr;

    addr = port->addrs.elts;
    last = port->addrs.nelts;
    /*
     * If there is a binding to an "*:port" then we need to bind() to
     * the "*:port" only and ignore other implicit bindings.  The bindings
     * have been already sorted: explicit bindings are on the start, then
     * implicit bindings go, and wildcard binding is in the end.
     */
    if (addr[last - 1].opt.wildcard) {
        addr[last - 1].opt.bind = 1;
        bind_wildcard = 1;//最后一个有通配符
    } else {
        bind_wildcard = 0;
    }
    i = 0;
    while (i < last) {
        if (bind_wildcard && !addr[i].opt.bind) {
            i++;
            continue;
        }

        ls = ngx_http_add_listening(cf, &addr[i]);//设置这个listening的回调handler，当接收一个连接后，会调用该回调初始化。
        if (ls == NULL) {//端口的创建，绑定操作在ngx_init_cycle里面，ngx_open_listening_sockets函数干的事情。
            return NGX_ERROR;
        }
        hport = ngx_pcalloc(cf->pool, sizeof(ngx_http_port_t));
        if (hport == NULL) {
            return NGX_ERROR;
        }
        ls->servers = hport;//指向所代表的server虚拟主机
        if (i == last - 1) {
            hport->naddrs = last;
        } else {
            hport->naddrs = 1;
            i = 0;
        }
		//下面主要初始化虚拟主机相关的地址结构，hash等，设置到hport指针里面，依据addr所指的配置数据。
        switch (ls->sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
        case AF_INET6:
            if (ngx_http_add_addrs6(cf, hport, addr) != NGX_OK) {
                return NGX_ERROR;
            }
            break;
#endif
        default: /* AF_INET */
            if (ngx_http_add_addrs(cf, hport, addr) != NGX_OK) {
                return NGX_ERROR;
            }
            break;
        }
        addr++;
        last--;
    }
    return NGX_OK;
}


static ngx_listening_t *
ngx_http_add_listening(ngx_conf_t *cf, ngx_http_conf_addr_t *addr)
{//创建一个HTTP的listening结构，其特点为handler为http特订的handler.
//当接收一个连接后，会调用listen SOCK的handler钩子告诉他做一写相关的初始化
//端口的打开和绑定在ngx_init_cycle里面进行。这里只做相关的初始化。
    ngx_listening_t           *ls;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;

    ls = ngx_create_listening(cf, &addr->opt.u.sockaddr, addr->opt.socklen);
    if (ls == NULL) {
        return NULL;
    }
    ls->addr_ntop = 1;
    ls->handler = ngx_http_init_connection;//监听完后，我需要进行自定义的回调，比如接受一个新连接后
    cscf = addr->default_server;//得到其默认的server的配置。
    ls->pool_size = cscf->connection_pool_size;
    ls->post_accept_timeout = cscf->client_header_timeout;//读取客户端请求头的超时时间，accept一个连接后，在ngx_http_init_connection里面会加入定时器
    clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    ls->logp = clcf->error_log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = ngx_accept_log_error;//对于监听的SOCK，注册日志的自定义打日志函数

    ls->backlog = addr->opt.backlog;//设置backlog
    ls->rcvbuf = addr->opt.rcvbuf;
    ls->sndbuf = addr->opt.sndbuf;

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    ls->accept_filter = addr->opt.accept_filter;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    ls->deferred_accept = addr->opt.deferred_accept;
#endif

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    ls->ipv6only = addr->opt.ipv6only;
#endif

#if (NGX_HAVE_SETFIB)
    ls->setfib = addr->opt.setfib;
#endif

    return ls;
}


static ngx_int_t
ngx_http_add_addrs(ngx_conf_t *cf, ngx_http_port_t *hport,  ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                 i;
    ngx_http_in_addr_t        *addrs;
    struct sockaddr_in        *sin;
    ngx_http_virtual_names_t  *vn;

    hport->addrs = ngx_pcalloc(cf->pool,   hport->naddrs * sizeof(ngx_http_in_addr_t));
    if (hport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs = hport->addrs;
    for (i = 0; i < hport->naddrs; i++) {
        sin = &addr[i].opt.u.sockaddr_in;//sin指向这个监听端口的地质结构
        addrs[i].addr = sin->sin_addr.s_addr;
        addrs[i].conf.default_server = addr[i].default_server;//赋值拷贝这个监听端口代表的默认虚拟主机的conf
#if (NGX_HTTP_SSL)
        addrs[i].conf.ssl = addr[i].opt.ssl;
#endif
        if (addr[i].hash.buckets == NULL  && (addr[i].wc_head == NULL  || addr[i].wc_head->hash.buckets == NULL)
            && (addr[i].wc_tail == NULL || addr[i].wc_tail->hash.buckets == NULL)
#if (NGX_PCRE)
            && addr[i].nregex == 0
#endif
            )
        {
            continue;
        }

        vn = ngx_palloc(cf->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            return NGX_ERROR;
        }
//下面拷贝配置的虚拟地址结构。表示这个端口都应的虚拟主机信息。
        addrs[i].conf.virtual_names = vn;
        vn->names.hash = addr[i].hash;
        vn->names.wc_head = addr[i].wc_head;
        vn->names.wc_tail = addr[i].wc_tail;
#if (NGX_PCRE)
        vn->nregex = addr[i].nregex;
        vn->regex = addr[i].regex;
#endif
    }

    return NGX_OK;
}


#if (NGX_HAVE_INET6)

static ngx_int_t
ngx_http_add_addrs6(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                 i;
    ngx_http_in6_addr_t       *addrs6;
    struct sockaddr_in6       *sin6;
    ngx_http_virtual_names_t  *vn;

    hport->addrs = ngx_pcalloc(cf->pool,
                               hport->naddrs * sizeof(ngx_http_in6_addr_t));
    if (hport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs6 = hport->addrs;

    for (i = 0; i < hport->naddrs; i++) {

        sin6 = &addr[i].opt.u.sockaddr_in6;
        addrs6[i].addr6 = sin6->sin6_addr;
        addrs6[i].conf.default_server = addr[i].default_server;
#if (NGX_HTTP_SSL)
        addrs6[i].conf.ssl = addr[i].opt.ssl;
#endif

        if (addr[i].hash.buckets == NULL
            && (addr[i].wc_head == NULL
                || addr[i].wc_head->hash.buckets == NULL)
            && (addr[i].wc_tail == NULL
                || addr[i].wc_tail->hash.buckets == NULL)
#if (NGX_PCRE)
            && addr[i].nregex == 0
#endif
            )
        {
            continue;
        }

        vn = ngx_palloc(cf->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            return NGX_ERROR;
        }

        addrs6[i].conf.virtual_names = vn;

        vn->names.hash = addr[i].hash;
        vn->names.wc_head = addr[i].wc_head;
        vn->names.wc_tail = addr[i].wc_tail;
#if (NGX_PCRE)
        vn->nregex = addr[i].nregex;
        vn->regex = addr[i].regex;
#endif
    }

    return NGX_OK;
}

#endif


char *
ngx_http_types_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_array_t     **types;
    ngx_str_t        *value, *default_type;
    ngx_uint_t        i, n, hash;
    ngx_hash_key_t   *type;

    types = (ngx_array_t **) (p + cmd->offset);

    if (*types == (void *) -1) {
        return NGX_CONF_OK;
    }

    default_type = cmd->post;

    if (*types == NULL) {
        *types = ngx_array_create(cf->temp_pool, 1, sizeof(ngx_hash_key_t));
        if (*types == NULL) {
            return NGX_CONF_ERROR;
        }

        if (default_type) {
            type = ngx_array_push(*types);
            if (type == NULL) {
                return NGX_CONF_ERROR;
            }

            type->key = *default_type;
            type->key_hash = ngx_hash_key(default_type->data,
                                          default_type->len);
            type->value = (void *) 4;
        }
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {

        if (value[i].len == 1 && value[i].data[0] == '*') {
            *types = (void *) -1;
            return NGX_CONF_OK;
        }

        hash = ngx_hash_strlow(value[i].data, value[i].data, value[i].len);
        value[i].data[value[i].len] = '\0';

        type = (*types)->elts;
        for (n = 0; n < (*types)->nelts; n++) {

            if (ngx_strcmp(value[i].data, type[n].key.data) == 0) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate MIME type \"%V\"", &value[i]);
                continue;
            }
        }

        type = ngx_array_push(*types);
        if (type == NULL) {
            return NGX_CONF_ERROR;
        }

        type->key = value[i];
        type->key_hash = hash;
        type->value = (void *) 4;
    }

    return NGX_CONF_OK;
}


char *
ngx_http_merge_types(ngx_conf_t *cf, ngx_array_t **keys, ngx_hash_t *types_hash,
    ngx_array_t **prev_keys, ngx_hash_t *prev_types_hash,
    ngx_str_t *default_types)
{
    ngx_hash_init_t  hash;

    if (*keys) {

        if (*keys == (void *) -1) {
            return NGX_CONF_OK;
        }

        hash.hash = types_hash;
        hash.key = NULL;
        hash.max_size = 2048;
        hash.bucket_size = 64;
        hash.name = "test_types_hash";
        hash.pool = cf->pool;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, (*keys)->elts, (*keys)->nelts) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    if (prev_types_hash->buckets == NULL) {

        if (*prev_keys == NULL) {

            if (ngx_http_set_default_types(cf, prev_keys, default_types)
                != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }

        } else if (*prev_keys == (void *) -1) {
            *keys = *prev_keys;
            return NGX_CONF_OK;
        }

        hash.hash = prev_types_hash;
        hash.key = NULL;
        hash.max_size = 2048;
        hash.bucket_size = 64;
        hash.name = "test_types_hash";
        hash.pool = cf->pool;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, (*prev_keys)->elts, (*prev_keys)->nelts)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    *types_hash = *prev_types_hash;

    return NGX_CONF_OK;

}


ngx_int_t
ngx_http_set_default_types(ngx_conf_t *cf, ngx_array_t **types,
    ngx_str_t *default_type)
{
    ngx_hash_key_t  *type;

    *types = ngx_array_create(cf->temp_pool, 1, sizeof(ngx_hash_key_t));
    if (*types == NULL) {
        return NGX_ERROR;
    }

    while (default_type->len) {

        type = ngx_array_push(*types);
        if (type == NULL) {
            return NGX_ERROR;
        }

        type->key = *default_type;
        type->key_hash = ngx_hash_key(default_type->data,
                                      default_type->len);
        type->value = (void *) 4;

        default_type++;
    }

    return NGX_OK;
}
