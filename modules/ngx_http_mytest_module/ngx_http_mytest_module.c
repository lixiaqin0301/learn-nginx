#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_files.h>


static char *ngx_http_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r);

static ngx_command_t ngx_http_mytest_commands[] = {

    {
        ngx_string("mytest"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
        ngx_http_mytest,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    ngx_null_command
};


static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,         /* preconfiguration */
    NULL,         /* postconfiguration */

    NULL,         /* create main configuration */
    NULL,         /* init main configuration */

    NULL,         /* create server configuration */
    NULL,         /* merge server configuration */

    NULL,         /* create location configuration */
    NULL          /* merge location configuration */
};


ngx_module_t ngx_http_mytest_module = {
    NGX_MODULE_V1,
    &ngx_http_mytest_module_ctx, /* module context */
    ngx_http_mytest_commands,    /* module directives */
    NGX_HTTP_MODULE,             /* module type */
    NULL,                        /* init master */
    NULL,                        /* init module */
    NULL,                        /* init process */
    NULL,                        /* init thread */
    NULL,                        /* exit thread */
    NULL,                        /* exit process */
    NULL,                        /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_http_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = NULL;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_mytest_handler;

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_mytest_handler(ngx_http_request_t *r)
{
    ngx_int_t                 rc = 0;
    ngx_buf_t                *b = NULL;
    ngx_chain_t               out;
    ngx_file_t               *file = NULL;
    ngx_file_info_t           fi;
    ngx_pool_cleanup_t       *cln = NULL;
    ngx_pool_cleanup_file_t  *clnf = NULL;
    ngx_str_t                 path = ngx_string("/tmp/mytest.txt");
    ngx_memzero(&out, sizeof(ngx_chain_t));
    ngx_memzero(&fi, sizeof(ngx_file_info_t));

    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));

    if (file == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    file->name = path;
    file->log = r->connection->log;

    file->fd = ngx_open_file(file->name.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

    if (file->fd == NGX_INVALID_FILE) {
        return NGX_HTTP_NOT_FOUND;
    }

    cln = ngx_pool_cleanup_add(r->pool, sizeof(ngx_pool_cleanup_file_t));

    if (cln == NULL) {
        ngx_close_file(file->fd);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cln->handler = ngx_pool_cleanup_file;
    clnf = cln->data;
    clnf->fd = file->fd;
    clnf->name = file->name.data;
    clnf->log = r->pool->log;

    if (ngx_fd_info(file->fd, &fi) == NGX_FILE_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = ngx_file_size(&fi);
    r->headers_out.last_modified_time = ngx_file_mtime(&fi);
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    b = ngx_calloc_buf(r->pool);

    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->file = file;
    b->file_pos = 0;
    b->file_last = ngx_file_size(&fi);

    b->in_file = b->file_last ? 1 : 0;
    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;
    b->sync = (b->last_buf || b->in_file) ? 0 : 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}
