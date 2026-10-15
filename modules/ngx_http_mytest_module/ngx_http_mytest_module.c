#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_MYTEST_SUBREQUESTS 3

typedef struct {
    ngx_str_t  code;
    ngx_str_t  price;
    ngx_str_t  volume;
    ngx_uint_t parsed;
    ngx_uint_t done;
} ngx_http_mytest_item_t;

typedef struct {
    ngx_uint_t             done;
    ngx_uint_t             sent;
    ngx_http_mytest_item_t items[NGX_HTTP_MYTEST_SUBREQUESTS];
} ngx_http_mytest_ctx_t;

typedef struct {
    ngx_uint_t             index;
    ngx_http_mytest_ctx_t *ctx;
} ngx_http_mytest_subrequest_t;

static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_mytest_post_subrequest(ngx_http_request_t *r, void *data, ngx_int_t rc);
static void ngx_http_mytest_parse(ngx_http_request_t *r, ngx_http_mytest_item_t *item);
static ngx_int_t ngx_http_mytest_field(u_char *p, u_char *last, ngx_uint_t n, ngx_str_t *field);
static ngx_int_t ngx_http_mytest_send(ngx_http_request_t *r, ngx_http_mytest_ctx_t *ctx);
static char *ngx_http_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_command_t ngx_http_mytest_commands[] = {
    {
        ngx_string("mytest"),
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
        ngx_http_mytest,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */
    NULL, /* create main configuration */
    NULL, /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* merge server configuration */
    NULL, /* create location configuration */
    NULL  /* merge location configuration */
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

static char *
ngx_http_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_mytest_handler;
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_mytest_handler(ngx_http_request_t *r)
{
    ngx_http_mytest_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_mytest_module);
    if (ctx != NULL) {
        if (ctx->sent || ctx->done < NGX_HTTP_MYTEST_SUBREQUESTS) {
            return NGX_AGAIN;
        }
        ctx->sent = 1;
        return ngx_http_mytest_send(r, ctx);
    }
    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_mytest_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_http_set_ctx(r, ctx, ngx_http_mytest_module);
    static ngx_str_t codes[NGX_HTTP_MYTEST_SUBREQUESTS] = {
        ngx_string("s_sh000001"),
        ngx_string("s_sh000002"),
        ngx_string("s_sh000003"),
    };
    for (ngx_uint_t i = 0; i < NGX_HTTP_MYTEST_SUBREQUESTS; i++) {
        ctx->items[i].code = codes[i];
        size_t len = sizeof("/mytest/") - 1 + codes[i].len;
        u_char *p = ngx_pnalloc(r->pool, len);
        if (p == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        ngx_memcpy(p, "/mytest/", sizeof("/mytest/") - 1);
        ngx_memcpy(p + sizeof("/mytest/") - 1, codes[i].data, codes[i].len);
        ngx_str_t uri = ngx_null_string;
        uri.data = p;
        uri.len = len;
        ngx_http_mytest_subrequest_t *sub = ngx_palloc(r->pool, sizeof(ngx_http_mytest_subrequest_t));
        ngx_http_post_subrequest_t *ps = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
        if (sub == NULL || ps == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        sub->ctx = ctx;
        sub->index = i;
        ps->handler = ngx_http_mytest_post_subrequest;
        ps->data = sub;
        ngx_http_request_t *sr = NULL;
        if (ngx_http_subrequest(r, &uri, NULL, &sr, ps, NGX_HTTP_SUBREQUEST_WAITED | NGX_HTTP_SUBREQUEST_IN_MEMORY) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }
    return NGX_AGAIN;
}

static ngx_int_t
ngx_http_mytest_post_subrequest(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    ngx_http_mytest_subrequest_t *sub = data;
    ngx_http_mytest_ctx_t *ctx = sub->ctx;
    ngx_http_mytest_item_t *item = &ctx->items[sub->index];
    if (item->done) {
        return rc;
    }
    if (rc == NGX_OK && r->headers_out.status == NGX_HTTP_OK) {
        ngx_http_mytest_parse(r, item);
    }
    item->done = 1;
    ++ctx->done;
    r->parent->write_event_handler = ngx_http_core_run_phases;
    return rc;
}

static void
ngx_http_mytest_parse(ngx_http_request_t *r, ngx_http_mytest_item_t *item)
{
    if (r->out == NULL || r->out->buf == NULL) {
        return;
    }
    u_char *p = r->out->buf->pos;
    u_char *last = r->out->buf->last;
    u_char *start = ngx_strlchr(p, last, '"');
    if (start == NULL) {
        return;
    }
    u_char *end = ngx_strlchr(++start, last, '"');
    if (end == NULL) {
        return;
    }
    if (ngx_http_mytest_field(start, end, 1, &item->price) != NGX_OK || ngx_http_mytest_field(start, end, 4, &item->volume) != NGX_OK) {
        return;
    }
    item->parsed = 1;
}

static ngx_int_t
ngx_http_mytest_field(u_char *p, u_char *last, ngx_uint_t n, ngx_str_t *field)
{
    for (u_char *start = p;; n--) {
        while (p < last && *p != ',') {
            p++;
        }
        if (n == 0) {
            field->data = start;
            field->len = p - start;
            return NGX_OK;
        }
        if (p == last) {
            return NGX_DECLINED;
        }
        start = ++p;
    }
    return NGX_ERROR;
}

static ngx_int_t
ngx_http_mytest_send(ngx_http_request_t *r, ngx_http_mytest_ctx_t *ctx)
{
    ngx_chain_t *out = ngx_pcalloc(r->pool, (NGX_HTTP_MYTEST_SUBREQUESTS + 1) * sizeof(ngx_chain_t));
    if (out == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    off_t total = 0;
    for (ngx_uint_t i = 0; i < NGX_HTTP_MYTEST_SUBREQUESTS; i++) {
        ngx_http_mytest_item_t *item = &ctx->items[i];
        if (!item->parsed) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        size_t len = sizeof("stock[], Today current price: , volume: ") - 1 + item->code.len + item->price.len + item->volume.len + 1; /* 末尾的换行 */
        ngx_buf_t *b = ngx_create_temp_buf(r->pool, len);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->last = ngx_slprintf(b->last, b->end, "stock[%V], Today current price: %V, volume: %V", &item->code, &item->price, &item->volume);
        *b->last++ = LF;
        out[i].buf = b;
        out[i].next = &out[i + 1];
        total += b->last - b->pos;
    }
    out[NGX_HTTP_MYTEST_SUBREQUESTS - 1].next = NULL;
    out[NGX_HTTP_MYTEST_SUBREQUESTS - 1].buf->last_buf = 1;
    out[NGX_HTTP_MYTEST_SUBREQUESTS - 1].buf->last_in_chain = 1;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = total;
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *)"text/plain";
    ngx_int_t rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
    return ngx_http_output_filter(r, out);
}
