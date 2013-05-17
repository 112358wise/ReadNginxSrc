
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_EVENT_PIPE_H_INCLUDED_
#define _NGX_EVENT_PIPE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


typedef struct ngx_event_pipe_s  ngx_event_pipe_t;

typedef ngx_int_t (*ngx_event_pipe_input_filter_pt)(ngx_event_pipe_t *p,
                                                    ngx_buf_t *buf);
typedef ngx_int_t (*ngx_event_pipe_output_filter_pt)(void *data,
                                                     ngx_chain_t *chain);


struct ngx_event_pipe_s {
    ngx_connection_t  *upstream;//��ʾnginx��client���Լ��ͺ�˵���������
    ngx_connection_t  *downstream;//�����ʾ�ͻ��˵�����

    ngx_chain_t       *free_raw_bufs;//�����˴�upstream��ȡ������(û�о����κδ����)���Լ������buf.
    ngx_chain_t       *in;//ÿ�ζ�ȡ���ݺ󣬵���input_filter��Э���ʽ���н����������������ݲ��ַŵ�in�����γ�һ������
    /*����p->in��shadow����˵һ�£�inָ��һ��chain����ÿ������ָ��һ��ʵʵ���ڵ�fcgi DATA���ݣ����������html����鹲��һ������FCGI���ݿ飻
    ����ĳ�������FCGI���ݿ�����һ�����ݽڵ��last_shadow��ԱΪ1����ʾ���������FCGI���ݿ�����һ���������ҵ�shadowָ��ָ�������FCGI���ݿ��bufָ��
	�ͷ���Щ�����ݿ��ʱ�򣬿��Բο�ngx_event_pipe_drain_chains�����ͷš�
    */
    ngx_chain_t      **last_in;//�����in�ṹ�����һ���ڵ��nextָ��ĵ�ַ��p->last_in = &cl->next;�������Ϳ��Խ��·�������FCGI�������ӵ������ˡ�

    ngx_chain_t       *out;//buf��tempfile�����ݻ�ŵ�out���档��ngx_event_pipe_write_chain_to_temp_file�����������õġ�
    ngx_chain_t      **last_out;

    ngx_chain_t       *free;
    ngx_chain_t       *busy;

    /*
     * the input filter i.e. that moves HTTP/1.1 chunks
     * from the raw bufs to an incoming chain
     *///FCGIΪngx_http_fastcgi_input_filter������Ϊngx_event_pipe_copy_input_filter �����������ض���ʽ����
    ngx_event_pipe_input_filter_pt    input_filter;//�������������ӦЭ������ݡ��������FCGIЭ������ݡ�
    void                             *input_ctx;

    ngx_event_pipe_output_filter_pt   output_filter;//ngx_http_output_filter���filter
    void                             *output_ctx;

    unsigned           read:1;//����Ƿ�������ݡ�
    unsigned           cacheable:1;
    unsigned           single_buf:1;//���ʹ����NGX_USE_AIO_EVENT�첽IO��־��������Ϊ1
    unsigned           free_bufs:1;
    unsigned           upstream_done:1;
    unsigned           upstream_error:1;
    unsigned           upstream_eof:1;
    unsigned           upstream_blocked:1;//ngx_event_pipe��������Ƿ��ȡ��upstream�������������ǲ���Ҫwrite
    unsigned           downstream_done:1;
    unsigned           downstream_error:1;
    unsigned           cyclic_temp_file:1;

    ngx_int_t          allocated;//��ʾ�Ѿ������˵�bufs�ĸ�����ÿ�λ�++
    ngx_bufs_t         bufs;//fastcgi_buffers��ָ�����õ�nginx��������body���ڴ����Ŀ�Լ���С��ngx_conf_set_bufs_slot������������������á�
    				//��Ӧxxx_buffers,Ҳ���Ƕ�ȡ��˵�����ʱ��bufer��С�Լ�����
    ngx_buf_tag_t      tag;

    ssize_t            busy_size;

    off_t              read_length;//��upstream��ȡ�����ݳ���

    off_t              max_temp_file_size;
    ssize_t            temp_file_write_size;

    ngx_msec_t         read_timeout;
    ngx_msec_t         send_timeout;
    ssize_t            send_lowat;

    ngx_pool_t        *pool;
    ngx_log_t         *log;

    ngx_chain_t       *preread_bufs;//ָ��ȡupstream��ʱ�����ģ�����˵Ԥ����body�������ݡ�p->preread_bufs->buf = &u->buffer;
    size_t             preread_size;
    ngx_buf_t         *buf_to_file;

    ngx_temp_file_t   *temp_file;

    /* STUB */ int     num;
};


ngx_int_t ngx_event_pipe(ngx_event_pipe_t *p, ngx_int_t do_write);
ngx_int_t ngx_event_pipe_copy_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf);
ngx_int_t ngx_event_pipe_add_free_buf(ngx_event_pipe_t *p, ngx_buf_t *b);


#endif /* _NGX_EVENT_PIPE_H_INCLUDED_ */
