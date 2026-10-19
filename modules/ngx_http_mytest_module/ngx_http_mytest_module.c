#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_flag_t enable;
} ngx_http_mytest_loc_conf_t;

typedef struct {
    ngx_uint_t add_prefix;
    ngx_uint_t add_suffix;
} ngx_http_mytest_ctx_t;

static ngx_str_t ngx_http_mytest_prefix = ngx_string("Hello Begin Mytest\n");
static ngx_str_t ngx_http_mytest_suffix = ngx_string("\nHello End Mytest\n");

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

static ngx_int_t ngx_http_mytest_filter_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_mytest_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_mytest_body_filter(ngx_http_request_t *r, ngx_chain_t *in);
static void *ngx_http_mytest_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_command_t ngx_http_mytest_commands[] = {
    {
        ngx_string("mytest"),
        NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t,
        enable),
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,                            /* preconfiguration */
    ngx_http_mytest_filter_init,     /* postconfiguration */
    NULL,                            /* create main configuration */
    NULL,                            /* init main configuration */
    NULL,                            /* create server configuration */
    NULL,                            /* merge server configuration */
    ngx_http_mytest_create_loc_conf, /* create location configuration */
    ngx_http_mytest_merge_loc_conf   /* merge location configuration */
};

ngx_module_t ngx_http_mytest_module = { NGX_MODULE_V1, &ngx_http_mytest_module_ctx, /* module context */
    ngx_http_mytest_commands, /* module directives */
    NGX_HTTP_MODULE,          /* module type */
    NULL,                     /* init master */
    NULL,                     /* init module */
    NULL,                     /* init process */
    NULL,                     /* init thread */
    NULL,                     /* exit thread */
    NULL,                     /* exit process */
    NULL,                     /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_mytest_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_mytest_header_filter;
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_mytest_body_filter;
    return NGX_OK;
}

static ngx_int_t
ngx_http_mytest_header_filter(ngx_http_request_t *r)
{
    ngx_http_mytest_loc_conf_t *lcf = ngx_http_get_module_loc_conf(r, ngx_http_mytest_module);
    if (!lcf->enable || r->header_only || r->headers_out.status != NGX_HTTP_OK || r->headers_out.content_length_n == 0) {
        return ngx_http_next_header_filter(r);
    }
    ngx_table_elt_t *h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    h->hash = 1;
    ngx_str_set(&h->key, "Debug-Mytest");
    ngx_str_set(&h->value, "Hello Mytest");
    ngx_http_mytest_ctx_t *ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_mytest_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    ctx->add_prefix = 1;
    ngx_http_set_ctx(r, ctx, ngx_http_mytest_module);
    if (r == r->main) {
        ngx_http_clear_content_length(r);
        ngx_http_clear_last_modified(r);
        ngx_http_clear_etag(r);
    }
    return ngx_http_next_header_filter(r);
}

static ngx_int_t
ngx_http_mytest_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_mytest_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_mytest_module);
    if (ctx == NULL || in == NULL || r->header_only) {
        return ngx_http_next_body_filter(r, in);
    }
    ngx_chain_t *head = NULL;
    ngx_chain_t **ll = &head;
    for (ngx_chain_t *cl = in; cl; cl = cl->next) {
        if (ctx->add_prefix) {
            ctx->add_prefix = 0;
            ngx_buf_t *b = ngx_calloc_buf(r->pool);
            ngx_chain_t *out = ngx_alloc_chain_link(r->pool);
            if (b == NULL || out == NULL) {
                return NGX_ERROR;
            }
            b->pos = ngx_http_mytest_prefix.data;
            b->last = b->pos + ngx_http_mytest_prefix.len;
            b->memory = 1;
            out->buf = b;
            out->next = NULL;
            *ll = out;
            ll = &out->next;
        }
        if (cl->buf->last_buf || cl->buf->last_in_chain) {
            cl->buf->last_buf = 0;
            cl->buf->last_in_chain = 0;
            ctx->add_suffix = 1;
        }
        ngx_chain_t *out = ngx_alloc_chain_link(r->pool);
        if (out == NULL) {
            return NGX_ERROR;
        }
        out->buf = cl->buf;
        out->next = NULL;
        *ll = out;
        ll = &out->next;
    }
    if (ctx->add_suffix) {
        ctx->add_suffix = 0;
        ngx_buf_t *b = ngx_calloc_buf(r->pool);
        ngx_chain_t *out = ngx_alloc_chain_link(r->pool);
        if (b == NULL || out == NULL) {
            return NGX_ERROR;
        }
        b->pos = ngx_http_mytest_suffix.data;
        b->last = b->pos + ngx_http_mytest_suffix.len;
        b->memory = 1;
        b->last_buf = (r == r->main) ? 1 : 0;
        b->last_in_chain = 1;
        out->buf = b;
        out->next = NULL;
        *ll = out;
    }
    return ngx_http_next_body_filter(r, head);
}

static void *
ngx_http_mytest_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_mytest_loc_conf_t *mlcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_mytest_loc_conf_t));
    if (mlcf != NULL) {
        mlcf->enable = NGX_CONF_UNSET;
    }
    return mlcf;
}

static char *
ngx_http_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_mytest_loc_conf_t *prev = parent;
    ngx_http_mytest_loc_conf_t *conf = child;
    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    return NGX_CONF_OK;
}
