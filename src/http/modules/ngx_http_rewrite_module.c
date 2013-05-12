
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t  *codes;/* uintptr_t *///�˽ṹ������code_t��ʵ�ֺ������飬�������ض���ġ�
						//�����Ǿ��������ɵģ��߼��Ϸ�Ϊһ��һ��ģ�ÿ��rewrite���ռ��һ�飬ÿһ����ܰ����ü���code()����ָ������ݡ�
						//���ƥ��ʧ�ܾ�ͨ��next�������顣
    ngx_uint_t    stack_size;//�����ʲô?���������Ҳ�������������e->sp[]����Ĵ�С���������洢�������ʱ��������ƶ�ջ����ʱֵ��

    ngx_flag_t    log;
    ngx_flag_t    uninitialized_variable_warn;
} ngx_http_rewrite_loc_conf_t;


static void *ngx_http_rewrite_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_rewrite_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_rewrite_init(ngx_conf_t *cf);
static char *ngx_http_rewrite(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_rewrite_return(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_break(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_if(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char * ngx_http_rewrite_if_condition(ngx_conf_t *cf,
    ngx_http_rewrite_loc_conf_t *lcf);
static char *ngx_http_rewrite_variable(ngx_conf_t *cf,
    ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value);
static char *ngx_http_rewrite_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char * ngx_http_rewrite_value(ngx_conf_t *cf,
    ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value);


static ngx_command_t  ngx_http_rewrite_commands[] = {

    { ngx_string("rewrite"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_TAKE23,
      ngx_http_rewrite,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("return"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_TAKE12,
      ngx_http_rewrite_return,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("break"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_NOARGS,
      ngx_http_rewrite_break,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("if"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_BLOCK|NGX_CONF_1MORE,
      ngx_http_rewrite_if,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("set"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_TAKE2,
      ngx_http_rewrite_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("rewrite_log"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF
                        |NGX_HTTP_LIF_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_rewrite_loc_conf_t, log),
      NULL },

    { ngx_string("uninitialized_variable_warn"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF
                        |NGX_HTTP_LIF_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_rewrite_loc_conf_t, uninitialized_variable_warn),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_rewrite_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_rewrite_init,                 /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_rewrite_create_loc_conf,      /* create location configration */
    ngx_http_rewrite_merge_loc_conf        /* merge location configration */
};


ngx_module_t  ngx_http_rewrite_module = {
    NGX_MODULE_V1,
    &ngx_http_rewrite_module_ctx,          /* module context */
    ngx_http_rewrite_commands,             /* module directives */
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

/*ngx_http_rewrite_init�����ڳ�ʼ��ʱ�������������SERVER_REWRITE_PHASE �� REWRITE_PHASE�����С�
//����ÿ�ν���ngx_core_run_phrases()����������ط������ض���
�ض�����Ϻ��������break���ͽ�������һ��find config �׶Σ�
���߳ɹ����ֽ��������µ�rewrite�������и��µ�������һ����
*/
static ngx_int_t ngx_http_rewrite_handler(ngx_http_request_t *r)
{
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t     *e;
    ngx_http_rewrite_loc_conf_t  *rlcf;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_http_rewrite_module);
    if (rlcf->codes == NULL) {//���û�д�������ֱ�ӷ��أ���Ϊ���ģ��϶�û��һ��rewrite��Ҳ���ǲ���Ҫ
        return NGX_DECLINED;//�������OK�ʹ�������ϣ����ô���i��������������ˡ�
    }
	//�½�һ���ű����棬��ʼ����codes�Ľ�����
    e = ngx_pcalloc(r->pool, sizeof(ngx_http_script_engine_t));
    if (e == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	//�����stack_size�������������õ�/�������涼�Ҳ�����
	//������������ż�����м�����
    e->sp = ngx_pcalloc(r->pool,  rlcf->stack_size * sizeof(ngx_http_variable_value_t));
    if (e->sp == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    e->ip = rlcf->codes->elts;
    e->request = r;
    e->quote = 1;
    e->log = rlcf->log;
    e->status = NGX_DECLINED;
	/*����������: rewrite ^(.*)$ http://$http_host.mp4 break; �����ѭ����i�����ߵ�
	ngx_http_rewrite_handler
		1. ngx_http_script_regex_start_code ��������������ʽ��������ܳ��ȣ����õ���e����
			1.1 ngx_http_script_copy_len_code		7
			1.2 ngx_http_script_copy_var_len_code 	18
			1.3 ngx_http_script_copy_len_code		4	=== 29 
			
		2. ngx_http_script_copy_code		����"http://" ��e->buf
		3. ngx_http_script_copy_var_code	����"115.28.34.175:8881"
		4. ngx_http_script_copy_code 		����".mp4"
		5. ngx_http_script_regex_end_code
	*/

    while (*(uintptr_t *) e->ip) {//����ÿһ������ָ�룬�ֱ�������ǡ�
        code = *(ngx_http_script_code_pt *) e->ip;
        code(e);//ִ�ж�Ӧָ��ĺ���������if�ȣ�
    }
    if (e->status == NGX_DECLINED) {
        return NGX_DECLINED;
    }
    if (r->err_status == 0) {
        return e->status;
    }
    return r->err_status;
}


static ngx_int_t ngx_http_rewrite_var(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{//��ֻ��һ��Ĭ�ϵ�get_handler��ʵ���ϻ�����Ϊ��Ӧ�ĺ�������ngx_http_get_indexed_variable
//ngx_http_rewrite_set�����Ὣ�������Ϊ��ʼ��get_handler
    ngx_http_variable_t          *var;
    ngx_http_core_main_conf_t    *cmcf;
    ngx_http_rewrite_loc_conf_t  *rlcf;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_http_rewrite_module);
    if (rlcf->uninitialized_variable_warn == 0) {
        *v = ngx_http_variable_null_value;
        return NGX_OK;
    }
    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    var = cmcf->variables.elts;
    /*
     * the ngx_http_rewrite_module sets variables directly in r->variables,
     * and they should be handled by ngx_http_get_indexed_variable(),
     * so the handler is called only if the variable is not initialized
     */
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "using uninitialized \"%V\" variable", &var[data].name);
    *v = ngx_http_variable_null_value;
    return NGX_OK;
}


static void *
ngx_http_rewrite_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_rewrite_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_rewrite_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->stack_size = NGX_CONF_UNSET_UINT;
    conf->log = NGX_CONF_UNSET;
    conf->uninitialized_variable_warn = NGX_CONF_UNSET;
    return conf;
}


static char *
ngx_http_rewrite_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_rewrite_loc_conf_t *prev = parent;
    ngx_http_rewrite_loc_conf_t *conf = child;

    uintptr_t  *code;
    ngx_conf_merge_value(conf->log, prev->log, 0);
    ngx_conf_merge_value(conf->uninitialized_variable_warn,  prev->uninitialized_variable_warn, 1);
    ngx_conf_merge_uint_value(conf->stack_size, prev->stack_size, 10);

    if (conf->codes == NULL) {
        return NGX_CONF_OK;
    }
    if (conf->codes == prev->codes) {
        return NGX_CONF_OK;
    }
    code = ngx_array_push_n(conf->codes, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_CONF_ERROR;
    }
    *code = (uintptr_t) NULL;//���׷��һ��code���������Խ�����
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_rewrite_init(ngx_conf_t *cf)
{//��ngx_http_rewrite_handler������õ�SERVER_REWRITE_PHASE �� REWRITE_PHASE������ȥ��
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_rewrite_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_rewrite_handler;
    return NGX_OK;
}

/*
1. ����������ʽ����ȡ��ģʽ��������ģʽ����variables�ȣ�
2.	�������ĸ�����last,break�ȡ�
3.����ngx_http_script_compile��Ŀ���ַ�������Ϊ�ṹ����codes������飬�Ա����ʱ���м��㣻
4.���ݵ������Ľ��������lcf->codes �飬����rewriteʱ��һ����Ľ���ƥ�伴�ɡ�ʧ���Զ��������飬������һ��rewrite
*/
static char * ngx_http_rewrite(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//����"rewrite"ָ��ʱ�������
//����rewrite ^(/xyz/aa.*)$ http://$http_host/aa.mp4 break; 
    ngx_http_rewrite_loc_conf_t  *lcf = conf;

    ngx_str_t                         *value;
    ngx_uint_t                         last;
    ngx_regex_compile_t                rc;
    ngx_http_script_code_pt           *code;
    ngx_http_script_compile_t          sc;
    ngx_http_script_regex_code_t      *regex;
    ngx_http_script_regex_end_code_t  *regex_end;
    u_char                             errstr[NGX_MAX_CONF_ERRSTR];
	//�ڱ�ģ���codes��β��������Ӧ����һ���µ�ָ�����ͷ��������һ����ʼ�ص�ngx_http_script_regex_start_code
	//�����������ngx_http_script_regex_code_t�����һ����ԱcodeΪ������e->ipָ��ĺ���ָ�룬������code���õġ�
    regex = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_regex_code_t));
    if (regex == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memzero(regex, sizeof(ngx_http_script_regex_code_t));
    value = cf->args->elts;
    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    rc.pattern = value[1];//��¼ ^(/xyz/aa.*)$
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    /* TODO: NGX_REGEX_CASELESS */
	//����������ʽ����дngx_http_regex_t�ṹ�����ء���������������ģʽ�ȶ��������ˡ�
    regex->regex = ngx_http_regex_compile(cf, &rc);
    if (regex->regex == NULL) {
        return NGX_CONF_ERROR;
    }
	//ngx_http_script_regex_start_code����ƥ��������ʽ������Ŀ���ַ������Ȳ�����ռ䡣
	//��������Ϊ��һ��code���������Ŀ���ַ�����С��β������ngx_http_script_regex_end_code
    regex->code = ngx_http_script_regex_start_code;
    regex->uri = 1;
    regex->name = value[1];//��¼������ʽ

    if (value[2].data[value[2].len - 1] == '?') {//���Ŀ�������������ʺý�β����nginx���´�������������
        /* the last "?" drops the original arguments */
        value[2].len--;
    } else {
        regex->add_args = 1;//�Զ�׷�Ӳ�����
    }

    last = 0;
    if (ngx_strncmp(value[2].data, "http://", sizeof("http://") - 1) == 0
        || ngx_strncmp(value[2].data, "https://", sizeof("https://") - 1) == 0
        || ngx_strncmp(value[2].data, "$scheme", sizeof("$scheme") - 1) == 0)
    {//nginx�жϣ��������http://�ȿ�ͷ��rewrite���ʹ����ǿ����ض��򡣻���302����
        regex->status = NGX_HTTP_MOVED_TEMPORARILY;
        regex->redirect = 1;//���Ҫ��302�ض���
        last = 1;
    }

    if (cf->args->nelts == 4) {//�������Ĳ�����
        if (ngx_strcmp(value[3].data, "last") == 0) {
            last = 1;
        } else if (ngx_strcmp(value[3].data, "break") == 0) {
            regex->break_cycle = 1;//��Ҫbreak�����������˸�last�����𣬲ο�ngx_http_script_regex_start_code��
            //�����־��Ӱ����������ɹ�֮��Ĵ��룬����������һ��url_changed=0,Ҳ��ƭnginx˵��URLû�б仯��
            //�㲻����������find config phrase�ˡ���Ȼ�������������һ����һ�顣
            last = 1;
        } else if (ngx_strcmp(value[3].data, "redirect") == 0) {
            regex->status = NGX_HTTP_MOVED_TEMPORARILY;
            regex->redirect = 1;
            last = 1;
        } else if (ngx_strcmp(value[3].data, "permanent") == 0) {
            regex->status = NGX_HTTP_MOVED_PERMANENTLY;
            regex->redirect = 1;
            last = 1;
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[3]);
            return NGX_CONF_ERROR;
        }
    }

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.cf = cf;
    sc.source = &value[2];//�ַ��� http://$http_host/aa.mp4
    sc.lengths = &regex->lengths;//�����������������һЩ�����Ŀ���ַ������ȵĺ����ص������ϻ��������: ���� ���� ����
    sc.values = &lcf->codes;//����ģʽ�������
    sc.variables = ngx_http_script_variables_count(&value[2]);
    sc.main = regex;//���Ƕ���ı��ʽ�����������lengths�ȡ�
    sc.complete_lengths = 1;
    sc.compile_args = !regex->redirect;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    regex = sc.main;//������ô����ԭ���ǿ��������ı��ڴ��ַ��
    regex->size = sc.size;
    regex->args = sc.args;

    if (sc.variables == 0 && !sc.dup_capture) {//���û�б������Ǿͽ�lengths�ÿգ������Ͳ�������������������ֱ�ӽ����ַ�������codes
        regex->lengths = NULL;
    }
    regex_end = ngx_http_script_add_code(lcf->codes, sizeof(ngx_http_script_regex_end_code_t), &regex);
    if (regex_end == NULL) {
        return NGX_CONF_ERROR;
    }
	/*��������Ĵ��������rewrite����������µĺ����ṹ: rewrite ^(.*)$ http://$http_host.mp4 break;
	ngx_http_script_regex_start_code ��������������ʽ������lengths����ܳ��ȣ�����ռ䡣
			ngx_http_script_copy_len_code		7
			ngx_http_script_copy_var_len_code 	18
			ngx_http_script_copy_len_code		4	=== 29 

	ngx_http_script_copy_code		����"http://" ��e->buf
	ngx_http_script_copy_var_code	����"115.28.34.175:8881"
	ngx_http_script_copy_code 		����".mp4"
	ngx_http_script_regex_end_code
	*/

    regex_end->code = ngx_http_script_regex_end_code;//�����ص�����Ӧǰ��Ŀ�ʼ��
    regex_end->uri = regex->uri;
    regex_end->args = regex->args;
    regex_end->add_args = regex->add_args;//�Ƿ���Ӳ�����
    regex_end->redirect = regex->redirect;

    if (last) {//�ο����棬���rewrite ĩβ��last,break,�ȣ��Ͳ����ٴν�������������ˣ���ô���ͽ�code����Ϊ�ա�
        code = ngx_http_script_add_code(lcf->codes, sizeof(uintptr_t), &regex);
        if (code == NULL) {
            return NGX_CONF_ERROR;
        }
        *code = NULL;
    }
	//��һ�����������ĵ�ַ��
    regex->next = (u_char *) lcf->codes->elts + lcf->codes->nelts - (u_char *) regex;
    return NGX_CONF_OK;
}

/*Syntax:	return code [ text ]
			return code URL 
			return URL

�������ı��ʽΪ: 
	ngx_http_script_return_code �͹���һ��������������״̬�뻹�еڶ�������
					��ô����Ҫ���ýű�������н����ˡ�return��rewriteָ��Ľű�Ӧ�����ݽṻ��΢�е㲻ͬ��
*/
static char * ngx_http_rewrite_return(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_rewrite_loc_conf_t  *lcf = conf;

    u_char                            *p;
    ngx_str_t                         *value, *v;
    ngx_http_script_return_code_t     *ret;
    ngx_http_compile_complex_value_t   ccv;

    ret = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_return_code_t));
    if (ret == NULL) {
        return NGX_CONF_ERROR;
    }
    value = cf->args->elts;
    ngx_memzero(ret, sizeof(ngx_http_script_return_code_t));
    ret->code = ngx_http_script_return_code;
    p = value[1].data;
    ret->status = ngx_atoi(p, value[1].len);//����һ�������򵥵�������״̬����������ʧ�ܣ��Ǿ��Ǹ�url
    if (ret->status == (uintptr_t) NGX_ERROR) {
        if (cf->args->nelts == 2
            && (ngx_strncmp(p, "http://", sizeof("http://") - 1) == 0
                || ngx_strncmp(p, "https://", sizeof("https://") - 1) == 0
                || ngx_strncmp(p, "$scheme", sizeof("$scheme") - 1) == 0))
        {//�ڶ�������ΪURL���Ǿ�ֱ��302�ض����
            ret->status = NGX_HTTP_MOVED_TEMPORARILY;
            v = &value[1];
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid return code \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    } else {
        if (cf->args->nelts == 2) {//��1������������return code��ֱ�ӷ��أ�������һ�������ˡ�
            return NGX_CONF_OK;
        }
        v = &value[2];
    }
	//���滹��һ����������������ø��ˡ���Ҫ�������ʽ�ˡ���������lengths��û��
    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
    ccv.cf = cf;
    ccv.value = v;//����Ĳ����ַ�����Ҫ�������ַ�����
    ccv.complex_value = &ret->text;
	//���и��ӱ��ʽ�Ľ�������������ngx_http_script_compile�ű�������б��롣
	//�����﷨�Ƚ�����������lengths,codes�ȡ�Ȼ��Ӧ��ccv.complex_value = &ret->text;
    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_rewrite_break(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//�������ˣ�ûʲô�������͹�ͺͺ��һ��break;������ǣ�
//����ʽ�ܼ�: ����һ��code��lcf->codes���档Ȼ��������ص�Ϊngx_http_script_break_code��
//��ʵ�������Ҳ������һ������e->request->uri_changed = 0;�㶮�ġ�break��last�������ڴˡ�
    ngx_http_rewrite_loc_conf_t *lcf = conf;

    ngx_http_script_code_pt  *code;
    code = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_CONF_ERROR;
    }
    *code = ngx_http_script_break_code;

    return NGX_CONF_OK;
}

/* Syntax:	if ( condition ) { ... }


*/
static char * ngx_http_rewrite_if(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
//����if���͵������
    ngx_http_rewrite_loc_conf_t  *lcf = conf;

    void                         *mconf;
    char                         *rv;
    u_char                       *elts;
    ngx_uint_t                    i;
    ngx_conf_t                    save;
    ngx_http_module_t            *module;
    ngx_http_conf_ctx_t          *ctx, *pctx;
    ngx_http_core_loc_conf_t     *clcf, *pclcf;
    ngx_http_script_if_code_t    *if_code;
    ngx_http_rewrite_loc_conf_t  *nlcf;
	//ngx_http_conf_ctx_t����������ˣ���ʵif����������location�����ã���������ֽ��������õ����νڵ㡣
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {//����һ���µ������Ľṹ��
        return NGX_CONF_ERROR;
    }

    pctx = cf->ctx;//����֮ǰ�������ʽṹ
    ctx->main_conf = pctx->main_conf;//�����丸�ڵ��main_conf
    ctx->srv_conf = pctx->srv_conf;//�����丸�ڵ��srv_conf
    //��ʵif��ֻ��loc_conf��һ��������location

    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[i]->ctx;
        if (module->create_loc_conf) {//����ÿ��HTTPģ���create_loc_conf
            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                 return NGX_CONF_ERROR;
            }
            ctx->loc_conf[ngx_modules[i]->ctx_index] = mconf;
        }
    }

    pclcf = pctx->loc_conf[ngx_http_core_module.ctx_index];//���ڵ�ĺ���core loc����

    clcf = ctx->loc_conf[ngx_http_core_module.ctx_index];//if{}��ĺ���core loc���á�
    clcf->loc_conf = ctx->loc_conf;//��loc��������Ϊ�µ�
    clcf->name = pclcf->name;//���ֿ�������
    clcf->noname = 1;
	//��location��ԭ����ʲô?ΪʲôҪ�����?��ʵ��������һ�������loction���ýڵ�ɡ�
	//�ڸ��ڵ�������һ�������location�ڵ㡣
    if (ngx_http_add_location(cf, &pclcf->locations, clcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
	//�����ű�����
    if (ngx_http_rewrite_if_condition(cf, lcf) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }
	//��lcf->codes������һ���lcf=conf��Ҳ����if�ڵ�����á�����һ���ص���
    if_code = ngx_array_push_n(lcf->codes, sizeof(ngx_http_script_if_code_t));
    if (if_code == NULL) {
        return NGX_CONF_ERROR;
    }
	//׷��һ��code��������β��Ҳ����: ���ƥ��ɹ����滻loc_conf,
    if_code->code = ngx_http_script_if_code;//Ȼ�����ngx_http_update_location_config���¸������á�
    elts = lcf->codes->elts;
    /* the inner directives must be compiled to the same code array */
    nlcf = ctx->loc_conf[ngx_http_rewrite_module.ctx_index];
    nlcf->codes = lcf->codes;//����Ƚ�����˼����if���������rewrite��һϵ�е�codes�ϲ������ڵ��codes��
    //Ȼ���ں�������nextָ���ʱ��if_code->next���Ŀ��������if��䡣��Ȼ��if�������Щָ����ʵҲ���нṹ�ġ�
    //������Ҳ��next��ֻ��ָ���ڲ���

    save = *cf;
    cf->ctx = ctx;//��ʱ�滻Ϊif{}���ctx�������Ϳ��Խ���ngx_conf_parse���������ˡ�

    if (pclcf->name.len == 0) {
        if_code->loc_conf = NULL;
        cf->cmd_type = NGX_HTTP_SIF_CONF;
    } else {
        if_code->loc_conf = ctx->loc_conf;
        cf->cmd_type = NGX_HTTP_LIF_CONF;
    }
    rv = ngx_conf_parse(cf, NULL);
    *cf = save;//��ԭΪ���ڵ�����á�
    if (rv != NGX_CONF_OK) {
        return rv;
    }

	//�������next��ת�Ƚ�����˼����������if�����codes���������ƶ���һ�㣬���ƴ��������ˡ�
	//�Ӷ��ﵽһ��Ч��: ���if û��ƥ��ͨ����һ��next��ת�����������ڵ�ĸ�if�������codes����������洦��
    if (elts != lcf->codes->elts) {
        if_code = (ngx_http_script_if_code_t *) ((u_char *) if_code + ((u_char *) lcf->codes->elts - elts));
    }
    if_code->next = (u_char *) lcf->codes->elts + lcf->codes->nelts - (u_char *) if_code;
    /* the code array belong to parent block */
    nlcf->codes = NULL;

    return NGX_CONF_OK;
}

/*Syntax:	if ( condition ) { ... }
	eg: if ($request_method = POST ) 
	����������˺ܶ๦��: 
	1.�������ƥ�� �� ngx_http_rewrite_variable������������ֵ������ngx_http_rewrite_value���������=/!=������ź���Ľű����ӱ��ʽ��ֵ��
		Ȼ������ngx_http_script_equal_code�ص������ƥ�䡣
	2.��������ƥ�䣬ͬ��ngx_http_rewrite_variable������������ֵ�󣬹���һ��ngx_http_script_regex_start_code������ƥ�䣻
	3.���ļ��������жϣ�����ngx_http_script_file_code��Ŀ¼���ļ����жϣ�

	�����������صĺ������ж���ɺ󶼽��Ὣ������ڶ�ջ���棬���ϲ���ص�ngx_http_script_if_code�����ж��Ƿ�ifƥ��ɹ���
	����ɹ������滻��ջ�����¸���loc_conf���á�
*/
static char * ngx_http_rewrite_if_condition(ngx_conf_t *cf, ngx_http_rewrite_loc_conf_t *lcf)
{
    u_char                        *p;
    size_t                         len;
    ngx_str_t                     *value;
    ngx_uint_t                     cur, last;
    ngx_regex_compile_t            rc;
    ngx_http_script_code_pt       *code;
    ngx_http_script_file_code_t   *fop;
    ngx_http_script_regex_code_t  *regex;
    u_char                         errstr[NGX_MAX_CONF_ERRSTR];

    value = cf->args->elts;
    last = cf->args->nelts - 1;
//������һϵ�в����Ϸ���У�顣
    if (value[1].len < 1 || value[1].data[0] != '(') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    if (value[1].len == 1) {//��һ����(���Ҿ�һ������
        cur = 2;
    } else {//���ź��ַ��������������̡�
        cur = 1;//��ǰ������ǵ�һ��������
        value[1].len--;
        value[1].data++;
    }
    if (value[last].len < 1 || value[last].data[value[last].len - 1] != ')') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[last]);
        return NGX_CONF_ERROR;
    }
    if (value[last].len == 1) {
        last--;
    } else {
        value[last].len--;
        value[last].data[value[last].len] = '\0';
    }

    len = value[cur].len;
    p = value[cur].data;

    if (len > 1 && p[0] == '$') {
        if (cur != last && cur + 2 != last) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[cur]);
            return NGX_CONF_ERROR;
        }
		//�������ֻ�ȡһ��������varialbes[]�е��±ꡣȻ��Ϊ������һ��code��lcf->codes��
		//code=ngx_http_script_var_code��ע�����������������������Ӷ�ջֵ�ġ�Ҳ���ǻᱣ���ջ�ġ�
        if (ngx_http_rewrite_variable(cf, lcf, &value[cur]) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;//��������lcf->codes[]��������������һ��code������ƥ�������ʱ����ȵ������ֵ��
        }
        if (cur == last) {//��һ��������
            return NGX_CONF_OK;
        }

        cur++;//������һ������

        len = value[cur].len;
        p = value[cur].data;

        if (len == 1 && p[0] == '=') {//�������Ǹ����ںţ���ô���һ�����������Ǹ�ֵ����������ȱȽϵġ�
        	//�����ַ�������������б����������ngx_http_script_compile���нű�������
        	//�����ַ�������������б����������ngx_http_script_compile���нű�������
			//����2����: 1.����codeΪngx_http_script_complex_value_code��2.���������value�ķ��ϱ��ʽ���������lengths,values,codes
            if (ngx_http_rewrite_value(cf, lcf, &value[last]) != NGX_CONF_OK) {
                return NGX_CONF_ERROR;
            }
            code = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(uintptr_t));
            if (code == NULL) {
                return NGX_CONF_ERROR;
            }
			/*Ȼ���ں������1���Ƚϲ�������������ɹ����͸ı��־�����ˡ�
			ע�⣬�����������������ʱ�򣬶�ջ�Ѿ�������2��ֵ�ˣ��ӵ�����Ϊ:ngx_http_rewrite_variable���������ֵ��
			ngx_http_script_complex_value_code���븴�ӱ��ʽƥ�������ֵ��������code���ÿ���ȡ��ջ�ϵ�1,2��λ�þ���Ҫ�Ƚϵ�2���ַ�����
			*/
            *code = ngx_http_script_equal_code;//����һ�����ƥ�䡣
            return NGX_CONF_OK;
        }
        if (len == 2 && p[0] == '!' && p[1] == '=') {
            if (ngx_http_rewrite_value(cf, lcf, &value[last]) != NGX_CONF_OK) {
                return NGX_CONF_ERROR;
            }
            code = ngx_http_script_start_code(cf->pool, &lcf->codes,  sizeof(uintptr_t));
            if (code == NULL) {
                return NGX_CONF_ERROR;
            }//ͬ�ϣ�ֻ�ǹ��ڵ���һ������ƥ��
            *code = ngx_http_script_not_equal_code;
            return NGX_CONF_OK;
        }

        if ((len == 1 && p[0] == '~')
            || (len == 2 && p[0] == '~' && p[1] == '*')
            || (len == 2 && p[0] == '!' && p[1] == '~')
            || (len == 3 && p[0] == '!' && p[1] == '~' && p[2] == '*'))
        {
        	//���ﲻ�����ˣ���Ϊ��Ҫ������ƥ������ģ���ô����ùҵ������ĺ����ˡ�
        	//ע�⣬���ﲻ��Ҫ������ƥ���rewrite����ͬ������ֻҪ֪���Ƿ�ƥ��ͨ�����С��������ַ�����
        	//��ˣ��������û��lengths,
            regex = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_regex_code_t));
            if (regex == NULL) {
                return NGX_CONF_ERROR;
            }
            ngx_memzero(regex, sizeof(ngx_http_script_regex_code_t));
            ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
            rc.pattern = value[last];//���һ������������ʽ��
            rc.options = (p[len - 1] == '*') ? NGX_REGEX_CASELESS : 0;//�����ִ�Сд
            rc.err.len = NGX_MAX_CONF_ERRSTR;
            rc.err.data = errstr;
            regex->regex = ngx_http_regex_compile(cf, &rc);//�������������ʽ������û�д���codes�ġ�
            //ע�������ngx_http_script_compile�����𣬺��������ű���������ģ�ngx_http_regex_compile��������ƥ��ġ�������ˡ�
            if (regex->regex == NULL) {
                return NGX_CONF_ERROR;
            }
			///�����������ƥ��ڵ㣬����ȴ�������幤������Ϊû��lengths,����û��codes�������˵ġ�
			//��������������ƥ��Ľڵ�͹��ˣ���Ϊ������test=1�󣬺������ڶ�ջ��������һ��ֵ������ngx_http_script_if_codeƥ������
            regex->code = ngx_http_script_regex_start_code;
            regex->next = sizeof(ngx_http_script_regex_code_t);
            regex->test = 1;//����Ҫ�����Ƿ�����ƥ��ɹ��������ƥ���ʱ��ǵ÷Ÿ���������ջ�
            if (p[0] == '!') {
                regex->negative_test = 1;
            }
            regex->name = value[last];
            return NGX_CONF_OK;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"%V\" in condition", &value[cur]);
        return NGX_CONF_ERROR;

    } else if ((len == 2 && p[0] == '-') || (len == 3 && p[0] == '!' && p[1] == '-'))
    {//����!-f operators;��
        if (cur + 1 != last) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[cur]);
            return NGX_CONF_ERROR;
        }

        value[last].data[value[last].len] = '\0';
        value[last].len++;
		//����϶��Ǹ������ˣ�Ҫ���������жϣ������ļ���Ŀ¼�Ƿ���ڣ��Ƿ���Ȩ�ޣ�������Ҫ������һ��������ֵ��
		//���value���������Ǹ��ӱ��ʽ������������ǹ��ڸ��ӱ��ʽ�����code.
		//����2����: 1.����codeΪngx_http_script_complex_value_code��2.���������value�ķ��ϱ��ʽ���������lengths,values,codes
        if (ngx_http_rewrite_value(cf, lcf, &value[last]) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;//����Ҳ�н����ˡ�
        }
        fop = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_file_code_t));
        if (fop == NULL) {
            return NGX_CONF_ERROR;
        }
        fop->code = ngx_http_script_file_code;//����һ���ļ��Ƿ���ڵ��жϡ�
        if (p[1] == 'f') {
            fop->op = ngx_http_script_file_plain;
            return NGX_CONF_OK;
        }
        if (p[1] == 'd') {
            fop->op = ngx_http_script_file_dir;
            return NGX_CONF_OK;
        }
        if (p[1] == 'e') {
            fop->op = ngx_http_script_file_exists;
            return NGX_CONF_OK;
        }
        if (p[1] == 'x') {
            fop->op = ngx_http_script_file_exec;
            return NGX_CONF_OK;
        }
        if (p[0] == '!') {
            if (p[2] == 'f') {
                fop->op = ngx_http_script_file_not_plain;
                return NGX_CONF_OK;
            }
            if (p[2] == 'd') {
                fop->op = ngx_http_script_file_not_dir;
                return NGX_CONF_OK;
            }
            if (p[2] == 'e') {
                fop->op = ngx_http_script_file_not_exists;
                return NGX_CONF_OK;
            }
            if (p[2] == 'x') {
                fop->op = ngx_http_script_file_not_exec;
                return NGX_CONF_OK;
            }
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid condition \"%V\"", &value[cur]);
        return NGX_CONF_ERROR;
    }
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid condition \"%V\"", &value[cur]);
    return NGX_CONF_ERROR;
}


static char *
ngx_http_rewrite_variable(ngx_conf_t *cf, ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value)
{//�������ֻ�ȡһ��������varialbes[]�е��±ꡣȻ��Ϊ������һ��code��lcf->codes��
//���Ϊ������ȡ����ngx_http_script_var_code��
    ngx_int_t                    index;
    ngx_http_script_var_code_t  *var_code;

    value->len--;
    value->data++;
    index = ngx_http_get_variable_index(cf, value);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }
    var_code = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_var_code_t));
    if (var_code == NULL) {
        return NGX_CONF_ERROR;
    }
    var_code->code = ngx_http_script_var_code;
    var_code->index = index;
    return NGX_CONF_OK;
}

/*Syntax:	set $variable value
1. ��$variable���뵽����ϵͳ�У�cmcf->variables_keys->keys��cmcf->variables��


a. ���value�Ǽ��ַ�������ô����֮��lcf->codes�ͻ�׷�������ĵ�����: 
	ngx_http_script_value_code  ֱ�Ӽ��ַ���ָ��һ�¾��У������ÿ����ˡ�
b. ���value�Ǹ��ӵİ��������Ĵ�����ôlcf->codes�ͻ�׷�����µĽ�ȥ :
	ngx_http_script_complex_value_code  ����lengths��lcode��ȡ����ַ������ܳ��ȣ����������ڴ�
		lengths
	values��������ݱ��ʽ�Ĳ�ͬ����ͬ�� �ֱ�value����ĸ��ӱ��ʽ��ֳ��﷨��Ԫ������һ������ֵ�����ϲ���һ��
	ngx_http_script_set_var_code		���������ϲ��������ս�����õ�variables[]������ȥ��

*/
static char * ngx_http_rewrite_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_rewrite_loc_conf_t  *lcf = conf;
    ngx_int_t                            index;
    ngx_str_t                           *value;
    ngx_http_variable_t                 *v;
    ngx_http_script_var_code_t          *vcode;
    ngx_http_script_var_handler_code_t  *vhcode;

    value = cf->args->elts;
    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    value[1].len--;
    value[1].data++;
	//������������������������뵽cmcf->variables_keys->keys���档
    v = ngx_http_add_variable(cf, &value[1], NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }
	//������뵽cmcf->variables���棬���������±�
    index = ngx_http_get_variable_index(cf, &value[1]);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    if (v->get_handler == NULL
        && ngx_strncasecmp(value[1].data, (u_char *) "http_", 5) != 0
        && ngx_strncasecmp(value[1].data, (u_char *) "sent_http_", 10) != 0
        && ngx_strncasecmp(value[1].data, (u_char *) "upstream_http_", 14) != 0)
    {//����������������Ͽ�ͷ������get_handlerΪngx_http_rewrite_var��dataΪindex ��
        v->get_handler = ngx_http_rewrite_var;//����һ��Ĭ�ϵ�handler����ngx_http_variables_init_vars������ʵ�ǻὫ�����ı������úõġ�
        v->data = index;
    }
	//�����ַ�������������б����������ngx_http_script_compile���нű�������
	//����2����: 1.����codeΪngx_http_script_complex_value_code��2.���������value�ķ��ϱ��ʽ���������lengths,values,codes
    if (ngx_http_rewrite_value(cf, lcf, &value[2]) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }

    if (v->set_handler) {//�����û�м���set_handler�����ù�
        vhcode = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_var_handler_code_t));
        if (vhcode == NULL) {
            return NGX_CONF_ERROR;
        }
        vhcode->code = ngx_http_script_var_set_handler_code;
        vhcode->handler = v->set_handler;
        vhcode->data = v->data;
        return NGX_CONF_OK;
    }

    vcode = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_var_code_t));
    if (vcode == NULL) {
        return NGX_CONF_ERROR;
    }
	//��set $variable valueָ������һ��code��������ﱣ��ֵ���������ܽ����ֵ���������ˡ�
    vcode->code = ngx_http_script_set_var_code;
    vcode->index = (uintptr_t) index;
    return NGX_CONF_OK;
}

/* ��һ���ַ������ű��﷨����������Ϊ��lcf->codes����������Ϊ: 
a. ���value�Ǽ��ַ�������ô����֮��lcf->codes�ͻ�׷�������ĵ�����: 
	ngx_http_script_value_code  ֱ�Ӽ��ַ���ָ��һ�¾��У������ÿ����ˡ�
b. ���value�Ǹ��ӵİ��������Ĵ�����ôlcf->codes�ͻ�׷�����µĽ�ȥ :
	ngx_http_script_complex_value_code  ����lengths��lcode��ȡ����ַ������ܳ��ȣ����������ڴ�
		lengths
	values��������ݱ��ʽ�Ĳ�ͬ����ͬ�� �ֱ�value����ĸ��ӱ��ʽ��ֳ��﷨��Ԫ������һ������ֵ�����ϲ���һ��
	���ں�����ô�죬������Ӧ�ã��ǱȽ�ֵ�أ������������Ĺ��� ��
	����set ָ���i����ngx_http_script_set_var_code�������ñ���ֵ��ifָ�����ڱȽϺ���ngx_http_script_equal_code
*/
static char * ngx_http_rewrite_value(ngx_conf_t *cf, ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value)
{//�����ַ�������������б����������ngx_http_script_compile���нű�������
//�Խṹ��lengths�����complex->lengths��values()���㺯�����뵱ǰlocation��codes�С�
//ngx_http_script_complex_value_code�����ڽ���rewriteƥ���ʱ������lengths�еľ�������Ŀ���ַ������ȵġ�
    ngx_int_t                              n;
    ngx_http_script_compile_t              sc;
    ngx_http_script_value_code_t          *val;
    ngx_http_script_complex_value_code_t  *complex;

    n = ngx_http_script_variables_count(value);//��������ַ����ı�����Ŀ
    if (n == 0) {//���û�б������Ǹ����ַ������Ǿͼ��ˡ�
        val = ngx_http_script_start_code(cf->pool, &lcf->codes,  sizeof(ngx_http_script_value_code_t));
        if (val == NULL) {
            return NGX_CONF_ERROR;
        }
        n = ngx_atoi(value->data, value->len);
        if (n == NGX_ERROR) {//�����ַ�����ô
            n = 0;
        }
		//�򵥵��ַ�����������ֱ��ָ��һ�¾����ˣ�ʲô�ڴ���䣬������Ҫ����Ϊ����ѹ���Ͳ���Ҫ�����ڴ档
        val->code = ngx_http_script_value_code;//���ַ�����code.
        val->value = (uintptr_t) n;
        val->text_len = (uintptr_t) value->len;
        val->text_data = (uintptr_t) value->data;
        return NGX_CONF_OK;
    }
	//����$�ı�����nginx���������complex value��.��������һ���µ�codes��
    complex = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_complex_value_code_t));
    if (complex == NULL) {
        return NGX_CONF_ERROR;
    }
	//����һ��start code,��ָ�����顣������ngx_http_script_regex_start_code
    complex->code = ngx_http_script_complex_value_code;
    complex->lengths = NULL;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = value;
    sc.lengths = &complex->lengths;
    sc.values = &lcf->codes;
    sc.variables = n;
    sc.complete_lengths = 1;
	//����������׶Σ��������ò�ͬ��code���complex->lengths��&lcf->codes
    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
