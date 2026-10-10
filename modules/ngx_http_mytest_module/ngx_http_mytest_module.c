#include "ngx_hash.h"
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_http_upstream_conf_t upstream;
} ngx_http_mytest_loc_conf_t;

typedef struct {
    ngx_http_request_t *request;
} ngx_http_mytest_ctx_t;

static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r);
static void *ngx_http_mytest_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static char *ngx_http_mytest_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_mytest_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_mytest_init_upstream(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_mytest_init_peer(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_mytest_get_peer(ngx_peer_connection_t *pc, void *data);
static void ngx_http_mytest_free_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t state);
static void ngx_http_mytest_notify_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t type);
static ngx_int_t ngx_http_mytest_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_mytest_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_mytest_process_header(ngx_http_request_t *r);
static void ngx_http_mytest_abort_request(ngx_http_request_t *r);
static void ngx_http_mytest_finalize_request(ngx_http_request_t *r, ngx_int_t rc);
static ngx_int_t ngx_http_mytest_rewrite_redirect(ngx_http_request_t *r, ngx_table_elt_t *h, size_t prefix);
static ngx_int_t ngx_http_mytest_rewrite_cookie(ngx_http_request_t *r, ngx_table_elt_t *h);
static ngx_int_t ngx_http_mytest_input_filter_init(void *data);
static ngx_int_t ngx_http_mytest_input_filter(void *data, ssize_t bytes);
#if (NGX_HTTP_CACHE)
static ngx_int_t ngx_http_mytest_create_key(ngx_http_request_t *r);
#endif

static ngx_event_get_peer_pt    ngx_http_mytest_rr_get;
static ngx_event_free_peer_pt   ngx_http_mytest_rr_free;
static ngx_event_notify_peer_pt ngx_http_mytest_rr_notify;

static ngx_conf_bitmask_t ngx_http_mytest_next_upstream_masks[] = {
    { ngx_string("error"),          NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"),        NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_header"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("http_403"),       NGX_HTTP_UPSTREAM_FT_HTTP_403 },
    { ngx_string("http_404"),       NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("http_500"),       NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { ngx_string("http_502"),       NGX_HTTP_UPSTREAM_FT_HTTP_502 },
    { ngx_string("http_503"),       NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { ngx_string("http_504"),       NGX_HTTP_UPSTREAM_FT_HTTP_504 },
    { ngx_string("off"),            NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string,              0 }
};

static ngx_str_t ngx_http_mytest_hide_headers[] = {
    ngx_null_string
};

static ngx_command_t ngx_http_mytest_commands[] = {
    {
        ngx_string("mytest_upstream"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE1,
        ngx_http_mytest_upstream,
        NGX_HTTP_MAIN_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("mytest_pass"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
        ngx_http_mytest_pass,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("mytest_connect_timeout"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.connect_timeout),
        NULL
    },
    {
        ngx_string("mytest_send_timeout"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.send_timeout),
        NULL
    },
    {
        ngx_string("mytest_read_timeout"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.read_timeout),
        NULL
    },
    {
        ngx_string("mytest_buffer_size"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.buffer_size),
        &ngx_conf_size_nonzero_post
    },
    {
        ngx_string("mytest_buffering"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.buffering),
        NULL
    },
    {
        ngx_string("mytest_request_buffering"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.request_buffering),
        NULL
    },
    {
        ngx_string("mytest_next_upstream"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
        ngx_conf_set_bitmask_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, upstream.next_upstream),
        &ngx_http_mytest_next_upstream_masks
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,  /* preconfiguration */
    NULL,  /* postconfiguration */
    NULL,  /* create main configuration */
    NULL,  /* init main configuration */
    NULL,  /* create server configuration */
    NULL,  /* merge server configuration */
    ngx_http_mytest_create_loc_conf,   /* create location configuration */
    ngx_http_mytest_merge_loc_conf     /* merge location configuration */
};

ngx_module_t ngx_http_mytest_module = {
    NGX_MODULE_V1,
    &ngx_http_mytest_module_ctx,  /* module context */
    ngx_http_mytest_commands,     /* module directives */
    NGX_HTTP_MODULE,              /* module type */
    NULL,                         /* init master */
    NULL,                         /* init module */
    NULL,                         /* init process */
    NULL,                         /* init thread */
    NULL,                         /* exit thread */
    NULL,                         /* exit process */
    NULL,                         /* exit master */
    NGX_MODULE_V1_PADDING
};

static void *
ngx_http_mytest_create_loc_conf(ngx_conf_t *cf)
{
    ngx_log_error(NGX_LOG_NOTICE, cf->log, 0, "mytest: create_loc_conf()");
    ngx_http_mytest_loc_conf_t *conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_mytest_loc_conf_t));
    if (conf) {
        conf->upstream.local = NGX_CONF_UNSET_PTR;
        conf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
        conf->upstream.pass_headers = NGX_CONF_UNSET_PTR;
        conf->upstream.socket_keepalive = NGX_CONF_UNSET;
        conf->upstream.next_upstream_tries = NGX_CONF_UNSET_UINT;
        conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
        conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
        conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;
        conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;
        conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;
        conf->upstream.buffering = NGX_CONF_UNSET;
        conf->upstream.request_buffering = NGX_CONF_UNSET;
    }
    return conf;
}

static char *
ngx_http_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_mytest_loc_conf_t *prev = parent;
    ngx_http_mytest_loc_conf_t *conf = child;
    ngx_log_error(NGX_LOG_NOTICE, cf->log, 0, "mytest: merge_loc_conf()");
    ngx_conf_merge_ptr_value(conf->upstream.local, prev->upstream.local, NULL);
    ngx_conf_merge_value(conf->upstream.socket_keepalive, prev->upstream.socket_keepalive, 0);
    ngx_conf_merge_uint_value(conf->upstream.next_upstream_tries, prev->upstream.next_upstream_tries, 0);
    ngx_conf_merge_msec_value(conf->upstream.connect_timeout, prev->upstream.connect_timeout, 60000);
    ngx_conf_merge_msec_value(conf->upstream.send_timeout, prev->upstream.send_timeout, 60000);
    ngx_conf_merge_msec_value(conf->upstream.read_timeout, prev->upstream.read_timeout, 60000);
    ngx_conf_merge_msec_value(conf->upstream.next_upstream_timeout, prev->upstream.next_upstream_timeout, 0);
    ngx_conf_merge_size_value(conf->upstream.buffer_size, prev->upstream.buffer_size, (size_t)ngx_pagesize);
    ngx_conf_merge_value(conf->upstream.buffering, prev->upstream.buffering, 0);
    ngx_conf_merge_value(conf->upstream.request_buffering, prev->upstream.request_buffering, 1);
    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream, prev->upstream.next_upstream, (NGX_CONF_BITMASK_SET | NGX_HTTP_UPSTREAM_FT_ERROR | NGX_HTTP_UPSTREAM_FT_TIMEOUT));
    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET | NGX_HTTP_UPSTREAM_FT_OFF;
    }
    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }
    ngx_hash_init_t hash;
    ngx_memzero(&hash, sizeof(ngx_hash_init_t));
    hash.hash = &conf->upstream.hide_headers_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "mytest_hide_headers_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;
    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream, &prev->upstream, ngx_http_mytest_hide_headers, &hash) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}

static char *
ngx_http_mytest_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{
    ngx_str_t *value = cf->args->elts;
    ngx_url_t u;
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.host = value[1];
    u.no_resolve = 1;
    u.no_port = 1;
    ngx_http_upstream_srv_conf_t *uscf = ngx_http_upstream_add(cf, &u, NGX_HTTP_UPSTREAM_CREATE | NGX_HTTP_UPSTREAM_WEIGHT | NGX_HTTP_UPSTREAM_MAX_FAILS | NGX_HTTP_UPSTREAM_FAIL_TIMEOUT | NGX_HTTP_UPSTREAM_DOWN | NGX_HTTP_UPSTREAM_BACKUP);
    if (uscf == NULL) {
        return NGX_CONF_ERROR;
    }
    uscf->peer.init_upstream = ngx_http_mytest_init_upstream;
    ngx_http_conf_ctx_t *ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_http_conf_ctx_t *http_ctx = cf->ctx;
    ctx->main_conf = http_ctx->main_conf;
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    ctx->srv_conf[ngx_http_upstream_module.ctx_index] = uscf;
    uscf->srv_conf = ctx->srv_conf;
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    for (ngx_uint_t m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        ngx_http_module_t *module = cf->cycle->modules[m]->ctx;
        if (module->create_srv_conf) {
            void *mconf = module->create_srv_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->srv_conf[cf->cycle->modules[m]->ctx_index] = mconf;
        }
        if (module->create_loc_conf) {
            void *mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->loc_conf[cf->cycle->modules[m]->ctx_index] = mconf;
        }
    }
    uscf->servers = ngx_array_create(cf->pool, 4, sizeof(ngx_http_upstream_server_t));
    if (uscf->servers == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_conf_t pcf = *cf;
    cf->ctx = ctx;
    cf->cmd_type = NGX_HTTP_UPS_CONF;
    char *rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;
    if (rv != NGX_CONF_OK) {
        return rv;
    }
    if (uscf->servers->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "mytest_upstream \"%V\" 里没有 server", &value[1]);
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}


static char *
ngx_http_mytest_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_mytest_loc_conf_t *mlcf = conf;
    if (mlcf->upstream.upstream) {
        return "is duplicate";
    }
    ngx_http_core_loc_conf_t *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_mytest_handler;
    ngx_str_t *value = cf->args->elts;
    ngx_url_t u;
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = value[1];
    u.no_resolve = 1;
    u.no_port = 1;
    mlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (mlcf->upstream.upstream == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "upstream \"%V\" 未通过 mytest_upstream 定义", &value[1]);
        return NGX_CONF_ERROR;
    }
    ngx_log_error(NGX_LOG_NOTICE, cf->log, 0, "mytest: mytest_pass -> upstream \"%V\"", &value[1]);
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_mytest_init_upstream(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    ngx_log_error(NGX_LOG_NOTICE, cf->log, 0, "mytest: init_upstream() upstream=\"%V\"", &us->host);
    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }
    us->peer.init = ngx_http_mytest_init_peer;
    return NGX_OK;
}

static ngx_int_t
ngx_http_mytest_init_peer(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *us)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: init_peer() upstream=\"%V\"", &us->host);
    if (ngx_http_upstream_init_round_robin_peer(r, us) != NGX_OK) {
        return NGX_ERROR;
    }
    ngx_http_mytest_rr_get = r->upstream->peer.get;
    ngx_http_mytest_rr_free = r->upstream->peer.free;
    ngx_http_mytest_rr_notify = r->upstream->peer.notify;

    r->upstream->peer.get = ngx_http_mytest_get_peer;
    r->upstream->peer.free = ngx_http_mytest_free_peer;
    r->upstream->peer.notify = ngx_http_mytest_notify_peer;

    return NGX_OK;
}

static ngx_int_t
ngx_http_mytest_get_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_log_error(NGX_LOG_NOTICE, pc->log, 0, "mytest: peer.get() tries=%ui", pc->tries);
    if (ngx_http_mytest_rr_get) {
        return ngx_http_mytest_rr_get(pc, data);
    }
    return NGX_ERROR;
}

static void
ngx_http_mytest_free_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t state)
{
    ngx_log_error(NGX_LOG_NOTICE, pc->log, 0, "mytest: peer.free() state=0x%xi", state);
    if (ngx_http_mytest_rr_free) {
        ngx_http_mytest_rr_free(pc, data, state);
    }
}

static void
ngx_http_mytest_notify_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t type)
{
    ngx_log_error(NGX_LOG_NOTICE, pc->log, 0, "mytest: peer.notify() type=%ui", type);
    if (ngx_http_mytest_rr_notify) {
        ngx_http_mytest_rr_notify(pc, data, type);
    }
}

static ngx_int_t
ngx_http_mytest_handler(ngx_http_request_t *r)
{
    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_http_upstream_t *u = r->upstream;
    ngx_str_set(&u->schema, "mytest://");
    u->output.tag = (ngx_buf_tag_t)&ngx_http_mytest_module;
    ngx_http_mytest_loc_conf_t *mlcf = ngx_http_get_module_loc_conf(r, ngx_http_mytest_module);
    u->conf = &mlcf->upstream;
    u->create_request = ngx_http_mytest_create_request;
    u->reinit_request = ngx_http_mytest_reinit_request;
    u->process_header = ngx_http_mytest_process_header;
    u->abort_request = ngx_http_mytest_abort_request;
    u->finalize_request = ngx_http_mytest_finalize_request;
    u->rewrite_redirect = ngx_http_mytest_rewrite_redirect;
    u->rewrite_cookie = ngx_http_mytest_rewrite_cookie;
#if (NGX_HTTP_CACHE)
    u->create_key = ngx_http_mytest_create_key;
#endif
    ngx_http_mytest_ctx_t *ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_mytest_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ctx->request = r;
    ngx_http_set_ctx(r, ctx, ngx_http_mytest_module);
    u->input_filter_init = ngx_http_mytest_input_filter_init;
    u->input_filter = ngx_http_mytest_input_filter;
    u->input_filter_ctx = r;
    r->main->count++;
    ngx_http_upstream_init(r);
    return NGX_DONE;
}

static ngx_int_t
ngx_http_mytest_create_request(ngx_http_request_t *r)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: create_request() uri=\"%V\"", &r->unparsed_uri);
    ngx_str_t uri = r->unparsed_uri.len ? r->unparsed_uri : r->uri;
    if (uri.len == 0) {
        ngx_str_set(&uri, "/");
    }
    ngx_str_t host = ngx_null_string;
    if (r->headers_in.server.len) {
        host = r->headers_in.server;
    } else {
        ngx_str_set(&host, "localhost");
    }
    size_t len = sizeof("GET ") - 1 + uri.len + sizeof(" HTTP/1.0\r\n") - 1 + sizeof("Host: ") - 1 + host.len + sizeof("\r\n") - 1 + sizeof("Connection: close\r\n") - 1 + sizeof("\r\n") - 1;
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }
    ngx_chain_t *cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    cl->buf = b;
    cl->next = NULL;
    r->upstream->request_bufs = cl;
    b->last = ngx_cpymem(b->last, "GET ", sizeof("GET ") - 1);
    b->last = ngx_copy(b->last, uri.data, uri.len);
    b->last = ngx_cpymem(b->last, " HTTP/1.0\r\n", sizeof(" HTTP/1.0\r\n") - 1);
    b->last = ngx_cpymem(b->last, "Host: ", sizeof("Host: ") - 1);
    b->last = ngx_copy(b->last, host.data, host.len);
    b->last = ngx_cpymem(b->last, "\r\n", sizeof("\r\n") - 1);
    b->last = ngx_cpymem(b->last, "Connection: close\r\n", sizeof("Connection: close\r\n") - 1);
    b->last = ngx_cpymem(b->last, "\r\n", sizeof("\r\n") - 1);
    return NGX_OK;
}

static ngx_int_t
ngx_http_mytest_reinit_request(ngx_http_request_t *r)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: reinit_request() 换下一台后端");
    return NGX_OK;
}

static ngx_int_t
ngx_http_mytest_process_header(ngx_http_request_t *r)
{
    ngx_http_upstream_t *u = r->upstream;
    ngx_http_upstream_main_conf_t *umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: process_header() 已收到 %uz 字节", u->buffer.last - u->buffer.pos);
    u_char *last = u->buffer.last;
    u_char *p = NULL;
    for (p = u->buffer.pos; p < last; p++) {
        if (*p == LF) {
            goto status_found;
        }
    }
    return NGX_AGAIN;
    ngx_str_t line = ngx_null_string;
status_found:
    line.data = u->buffer.pos;
    line.len = p - u->buffer.pos;
    if (line.len == 0 || *(p - 1) != '\r') {
        goto invalid;
    }
    line.len--;
    if (line.len < sizeof("HTTP/1.0 200") - 1 || ngx_strncasecmp(line.data, (u_char *)"HTTP/1.", 7) != 0) {
        goto invalid;
    }
    u_char *start = line.data + sizeof("HTTP/1.") - 1;
    u_char *end = line.data + line.len;
    if (start < end && (*start == '0' || *start == '1')) {
        start++;
    }
    while (start < end && *start == ' ') {
        start++;
    }
    if (end - start < 3) {
        goto invalid;
    }
    u->headers_in.status_line.data = start;
    u->headers_in.status_line.len = end - start;
    ngx_uint_t status = ngx_atoi(start, 3);
    if (status == (ngx_uint_t)NGX_ERROR) {
        goto invalid;
    }
    u->headers_in.status_n = status;
    u->state->status = status;
    p++;
    u->buffer.pos = p;
    for (;;) {
        for (; p < last; p++) {
            if (*p == LF) {
                goto header_found;
            }
        }
        return NGX_AGAIN;
    header_found:
        line.data = u->buffer.pos;
        line.len = p - u->buffer.pos;
        if (line.len && *(p - 1) == '\r') {
            line.len--;
        }
        if (line.len == 0) {
            u->buffer.pos = p + 1;
            break;
        }
        start = line.data;
        end = line.data + line.len;
        ngx_str_t key = ngx_null_string;
        for (key.data = start; start < end && *start != ':'; start++) {
            ;
        }
        if (start == end) {
            goto invalid;
        }
        key.len = start - key.data;
        for (start++; start < end && (*start == ' ' || *start == '\t'); start++) {
            ;
        }

        ngx_str_t value = ngx_null_string;
        value.data = start;
        value.len = end - start;
        ngx_table_elt_t *h = ngx_list_push(&u->headers_in.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }
        h->key = key;
        h->value = value;
        h->hash = ngx_hash_key_lc(key.data, key.len);
        h->lowcase_key = ngx_pnalloc(r->pool, key.len);
        if (h->lowcase_key == NULL) {
            return NGX_ERROR;
        }
        ngx_strlow(h->lowcase_key, key.data, key.len);
        ngx_http_upstream_header_t *hh = ngx_hash_find(&umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
        if (hh) {
            if (hh->handler(r, h, hh->offset) != NGX_OK) {
                return NGX_ERROR;
            }
        }
        p++;
        u->buffer.pos = p;
    }
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: process_header() 解析完成 status=%ui", status);
    return NGX_OK;
invalid:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "mytest: 无法解析 upstream 的响应");
    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
}

static void
ngx_http_mytest_abort_request(ngx_http_request_t *r)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: abort_request()");
}

static void
ngx_http_mytest_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: finalize_request() rc=%i", rc);
}

static ngx_int_t
ngx_http_mytest_rewrite_redirect(ngx_http_request_t *r, ngx_table_elt_t *h, size_t prefix)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: rewrite_redirect() Location=\"%V\"", &h->value);
    return NGX_DECLINED;
}

static ngx_int_t
ngx_http_mytest_rewrite_cookie(ngx_http_request_t *r, ngx_table_elt_t *h)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: rewrite_cookie() Set-Cookie=\"%V\"", &h->value);
    return NGX_DECLINED;
}

static ngx_int_t
ngx_http_mytest_input_filter_init(void *data)
{
    ngx_http_request_t *r = data;
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: input_filter_init()");
    return ngx_http_upstream_non_buffered_filter_init(data);
}

static ngx_int_t
ngx_http_mytest_input_filter(void *data, ssize_t bytes)
{
    ngx_http_request_t *r = data;
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: input_filter() bytes=%z", bytes);
    return ngx_http_upstream_non_buffered_filter(data, bytes);
}

#if (NGX_HTTP_CACHE)

static ngx_int_t
ngx_http_mytest_create_key(ngx_http_request_t *r)
{
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "mytest: create_key()");
    return NGX_OK;
}

#endif
