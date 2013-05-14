
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_SCRIPT_H_INCLUDED_
#define _NGX_HTTP_SCRIPT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    u_char                     *ip;
	/*����pos && code: ÿ�ε���code,���Ὣ���������µ��ַ�������posָ����ַ�������
	Ȼ��pos����ƶ����´ν����ʱ�򣬻��Զ�������׷�ӵ�����ġ�
	����ipҲ�����ԭ��code����Ὣe->ip����ƶ����ƶ��Ĵ�С���ݲ�ͬ�ı���������ء�
	ipָ��һ���ڴ棬������Ϊ������ص�һ���ṹ�壬����ngx_http_script_copy_capture_code_t��
	�ṹ��֮��������һ��ip�ĵ�ַ�������ƶ�ʱ�������� :
	code = (ngx_http_script_copy_capture_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_copy_capture_code_t);//�ƶ���ô��λ�ơ�
	*/ 
    u_char                     *pos;//pos֮ǰ�����ݾ��ǽ����ɹ��ģ���������ݽ�׷�ӵ�pos���档
    ngx_http_variable_value_t  *sp;//����ò������sp�������м��������籣�浱ǰ��һ���Ľ��ȣ�����һ������e->sp--���ҵ���һ���Ľ����

    ngx_str_t                   buf;//��Ž����Ҳ����buffer��posָ�����С�
    ngx_str_t                   line;//��¼������URI  e->line = r->uri;

    /* the start of the rewritten arguments */
    u_char                     *args;

    unsigned                    flushed:1;
    unsigned                    skip:1;
    unsigned                    quote:1;
    unsigned                    is_args:1;
    unsigned                    log:1;

    ngx_int_t                   status;
    ngx_http_request_t         *request;//����������
} ngx_http_script_engine_t;


typedef struct {
    ngx_conf_t                 *cf;
    ngx_str_t                  *source;//ָ���ַ���������http://$http_host/aa.mp4

    ngx_array_t               **flushes;
    ngx_array_t               **lengths;//ָ���ⲿ�ı���������&index->lengths;��
    ngx_array_t               **values;

    ngx_uint_t                  variables;//sourceָ����ַ������м�������
    ngx_uint_t                  ncaptures;//����һ��$3 ������
    ngx_uint_t                  captures_mask;
    ngx_uint_t                  size;

    void                       *main;

    unsigned                    compile_args:1;
    unsigned                    complete_lengths:1;
    unsigned                    complete_values:1;
    unsigned                    zero:1;
    unsigned                    conf_prefix:1;
    unsigned                    root_prefix:1;

    unsigned                    dup_capture:1;
    unsigned                    args:1;
} ngx_http_script_compile_t;


typedef struct {
    ngx_str_t                   value;//Ҫ�������ַ�����
    ngx_uint_t                 *flushes;
    void                       *lengths;
    void                       *values;
} ngx_http_complex_value_t;


typedef struct {
    ngx_conf_t                 *cf;
    ngx_str_t                  *value;//����Ĳ����ַ�����Ҫ�������ַ�����
    ngx_http_complex_value_t   *complex_value;//���ӱ��ʽ��lcode,codes����Ľṹ���洢�˸��ӱ��ʽ�Ľ�����Ϣ��

    unsigned                    zero:1;
    unsigned                    conf_prefix:1;
    unsigned                    root_prefix:1;
} ngx_http_compile_complex_value_t;


typedef void (*ngx_http_script_code_pt) (ngx_http_script_engine_t *e);
typedef size_t (*ngx_http_script_len_code_pt) (ngx_http_script_engine_t *e);


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   len;
} ngx_http_script_copy_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   index;//������cmcf->variables�е��±�
} ngx_http_script_var_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    ngx_http_set_variable_pt    handler;
    uintptr_t                   data;
} ngx_http_script_var_handler_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   n;//�ڼ���capture�������˵�ֵ����������$1,$2,����Ѱ��r->captures������±꣬��2Ϊ��λ��
} ngx_http_script_copy_capture_code_t;


#if (NGX_PCRE)

typedef struct {
    ngx_http_script_code_pt     code;//��ǰ��code����һ��������Ϊngx_http_script_regex_start_code
    ngx_http_regex_t           *regex;//�������������ʽ��
    ngx_array_t                *lengths;//�����������ʽ��Ӧ��lengths�������������� �ڶ����� rewrite ^(.*)$ http://$http_host.mp4 break;
    									//lengths�������һϵ��code,������Ŀ��url�Ĵ�С�ġ�
    uintptr_t                   size;
    uintptr_t                   status;
    uintptr_t                   next;//next�ĺ���Ϊ;�����ǰcodeƥ��ʧ�ܣ���ô��һ��code��λ������ʲô�ط�����Щ����ȫ������һ����������ġ�

    uintptr_t                   test:1;//����Ҫ�����Ƿ�����ƥ��ɹ��������ƥ���ʱ��ǵ÷Ÿ���������ջ�
    uintptr_t                   negative_test:1;
    uintptr_t                   uri:1;//�Ƿ���URIƥ�䡣
    uintptr_t                   args:1;

    /* add the r->args to the new arguments */
    uintptr_t                   add_args:1;//�Ƿ��Զ�׷�Ӳ�����rewrite���档���Ŀ�������������ʺý�β����nginx���´�������������

    uintptr_t                   redirect:1;//nginx�жϣ��������http://�ȿ�ͷ��rewrite���ʹ����ǿ����ض��򡣻���302����
    uintptr_t                   break_cycle:1;
	//rewrite���Ĳ�����break����rewrite��ĵ�ַ�ڵ�ǰlocation��ǩ��ִ�С�����ο�ngx_http_script_regex_start_code

    ngx_str_t                   name;
} ngx_http_script_regex_code_t;


typedef struct {
    ngx_http_script_code_pt     code;

    uintptr_t                   uri:1;
    uintptr_t                   args:1;

    /* add the r->args to the new arguments */
    uintptr_t                   add_args:1;

    uintptr_t                   redirect:1;
} ngx_http_script_regex_end_code_t;

#endif


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   conf_prefix;
} ngx_http_script_full_name_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   status;//���ص�״̬�롣return code [ text ]
    ngx_http_complex_value_t    text;//ccv.complex_value = &ret->text;����Ĳ����Ľű������ַ��
} ngx_http_script_return_code_t;


typedef enum {
    ngx_http_script_file_plain = 0,
    ngx_http_script_file_not_plain,
    ngx_http_script_file_dir,
    ngx_http_script_file_not_dir,
    ngx_http_script_file_exists,
    ngx_http_script_file_not_exists,
    ngx_http_script_file_exec,
    ngx_http_script_file_not_exec
} ngx_http_script_file_op_e;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   op;
} ngx_http_script_file_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   next;
    void                      **loc_conf;//�µ�location���á�
} ngx_http_script_if_code_t;


typedef struct {
    ngx_http_script_code_pt     code;//ngx_http_script_complex_value_code
    ngx_array_t                *lengths;//����ָ������Ƕ��������code
} ngx_http_script_complex_value_code_t;


typedef struct {
    ngx_http_script_code_pt     code;//����Ϊngx_http_script_value_code
    uintptr_t                   value;//���ִ�С���������text_data�������ִ�����Ϊ0.
    uintptr_t                   text_len;//���ַ����ĳ��ȡ�
    uintptr_t                   text_data;//��¼�ַ�����ַvalue->data;
} ngx_http_script_value_code_t;


void ngx_http_script_flush_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val);
ngx_int_t ngx_http_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_str_t *value);
ngx_int_t ngx_http_compile_complex_value(ngx_http_compile_complex_value_t *ccv);
char *ngx_http_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


ngx_int_t ngx_http_test_predicates(ngx_http_request_t *r,
    ngx_array_t *predicates);
char *ngx_http_set_predicate_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

ngx_uint_t ngx_http_script_variables_count(ngx_str_t *value);
ngx_int_t ngx_http_script_compile(ngx_http_script_compile_t *sc);
u_char *ngx_http_script_run(ngx_http_request_t *r, ngx_str_t *value,
    void *code_lengths, size_t reserved, void *code_values);
void ngx_http_script_flush_no_cacheable_variables(ngx_http_request_t *r,
    ngx_array_t *indices);

void *ngx_http_script_start_code(ngx_pool_t *pool, ngx_array_t **codes,
    size_t size);
void *ngx_http_script_add_code(ngx_array_t *codes, size_t size, void *code);

size_t ngx_http_script_copy_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_copy_var_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_var_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_copy_capture_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_capture_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_mark_args_code(ngx_http_script_engine_t *e);
void ngx_http_script_start_args_code(ngx_http_script_engine_t *e);
#if (NGX_PCRE)
void ngx_http_script_regex_start_code(ngx_http_script_engine_t *e);
void ngx_http_script_regex_end_code(ngx_http_script_engine_t *e);
#endif
void ngx_http_script_return_code(ngx_http_script_engine_t *e);
void ngx_http_script_break_code(ngx_http_script_engine_t *e);
void ngx_http_script_if_code(ngx_http_script_engine_t *e);
void ngx_http_script_equal_code(ngx_http_script_engine_t *e);
void ngx_http_script_not_equal_code(ngx_http_script_engine_t *e);
void ngx_http_script_file_code(ngx_http_script_engine_t *e);
void ngx_http_script_complex_value_code(ngx_http_script_engine_t *e);
void ngx_http_script_value_code(ngx_http_script_engine_t *e);
void ngx_http_script_set_var_code(ngx_http_script_engine_t *e);
void ngx_http_script_var_set_handler_code(ngx_http_script_engine_t *e);
void ngx_http_script_var_code(ngx_http_script_engine_t *e);
void ngx_http_script_nop_code(ngx_http_script_engine_t *e);


#endif /* _NGX_HTTP_SCRIPT_H_INCLUDED_ */
