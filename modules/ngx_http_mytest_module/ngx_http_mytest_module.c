#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_files.h>

typedef struct {
    ngx_str_t key;
    ngx_int_t value;
} ngx_http_mytest_pair_t;

typedef struct {
    ngx_flag_t   flag;       // ngx_conf_set_flag_slot
    ngx_str_t    str;        // ngx_conf_set_str_slot
    ngx_array_t *str_array;  // ngx_conf_set_str_array_slot
    ngx_array_t *keyval;     // ngx_conf_set_keyval_slot
    ngx_int_t    num;        // ngx_conf_set_num_slot
    size_t       size;       // ngx_conf_set_size_slot
    off_t        off;        // ngx_conf_set_off_slot
    ngx_msec_t   msec;       // ngx_conf_set_msec_slot
    time_t       sec;        // ngx_conf_set_sec_slot
    ngx_bufs_t   bufs;       // ngx_conf_set_bufs_slot
    ngx_uint_t   enum_val;   // ngx_conf_set_enum_slot
    ngx_uint_t   bitmask;    // ngx_conf_set_bitmask_slot
    ngx_path_t  *path;       // ngx_conf_set_path_slot
    ngx_uint_t   access;     // ngx_conf_set_access_slot
    ngx_array_t *custom;     // 自定义 set 回调
} ngx_http_mytest_loc_conf_t;

static ngx_conf_enum_t ngx_http_mytest_enum[] = {
    { ngx_string("first"),  1 },
    { ngx_string("second"), 2 },
    { ngx_string("third"),  3 },
    { ngx_null_string,      0 }
};

static ngx_conf_bitmask_t ngx_http_mytest_bitmask[] = {
    { ngx_string("read"),   0x01 },
    { ngx_string("write"),  0x02 },
    { ngx_string("delete"), 0x04 },
    { ngx_null_string,      0 }
};

static ngx_path_init_t ngx_http_mytest_path = {
    ngx_string("/tmp/mytest_cache"),
    { 1, 2, 0 }
};

static ngx_conf_num_bounds_t ngx_http_mytest_num_bounds = {
    ngx_conf_check_num_bounds, 1, 100
};

static char *ngx_http_mytest_str_post(ngx_conf_t *cf, void *post, void *data);

static ngx_conf_post_t ngx_http_mytest_str_post_ctx = {
    ngx_http_mytest_str_post
};

static char *ngx_http_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_mytest_custom(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_http_mytest_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_mytest_dump_handler(ngx_http_request_t *r);

static ngx_command_t ngx_http_mytest_commands[] = {
    {
        ngx_string("mytest"),
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
        ngx_http_mytest,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("mytest_flag"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, flag),
        NULL
    },
    {
        ngx_string("mytest_str"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, str),
        &ngx_http_mytest_str_post_ctx
    },
    {
        ngx_string("mytest_str_array"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_array_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, str_array),
        NULL
    },
    {
        ngx_string("mytest_keyval"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
        ngx_conf_set_keyval_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, keyval),
        NULL
    },
    {
        ngx_string("mytest_num"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, num),
        &ngx_http_mytest_num_bounds
    },
    {
        ngx_string("mytest_size"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, size),
        NULL
    },
    {
        ngx_string("mytest_off"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_off_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, off),
        NULL
    },
    {
        ngx_string("mytest_msec"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, msec),
        NULL
    },
    {
        ngx_string("mytest_sec"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_sec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, sec),
        NULL
    },
    {
        ngx_string("mytest_bufs"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
        ngx_conf_set_bufs_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, bufs),
        NULL
    },
    {
        ngx_string("mytest_enum"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_enum_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, enum_val),
        ngx_http_mytest_enum
    },
    {
        ngx_string("mytest_bitmask"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_conf_set_bitmask_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, bitmask),
        ngx_http_mytest_bitmask
    },
    {
        ngx_string("mytest_path"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE123,
        ngx_conf_set_path_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, path),
        NULL
    },
    {
        ngx_string("mytest_access"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_conf_set_access_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_conf_t, access),
        NULL
    },
    {
        ngx_string("mytest_custom"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_http_mytest_custom,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,                             /* preconfiguration */
    NULL,                             /* postconfiguration */

    NULL,                             /* create main configuration */
    NULL,                             /* init main configuration */

    NULL,                             /* create server configuration */
    NULL,                             /* merge server configuration */

    ngx_http_mytest_create_loc_conf,  /* create location configuration */
    ngx_http_mytest_merge_loc_conf    /* merge location configuration */
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

static void *
ngx_http_mytest_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_mytest_loc_conf_t *mlcf;
    mlcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_mytest_loc_conf_t));
    if (mlcf) {
        mlcf->flag = NGX_CONF_UNSET;
        mlcf->str_array = NGX_CONF_UNSET_PTR;
        mlcf->keyval = NGX_CONF_UNSET_PTR;
        mlcf->num = NGX_CONF_UNSET;
        mlcf->size = NGX_CONF_UNSET_SIZE;
        mlcf->off = NGX_CONF_UNSET;
        mlcf->msec = NGX_CONF_UNSET_MSEC;
        mlcf->sec = NGX_CONF_UNSET;
        mlcf->enum_val = NGX_CONF_UNSET_UINT;
        mlcf->bitmask = 0;
        mlcf->path = NULL;
        mlcf->access = NGX_CONF_UNSET_UINT;
        mlcf->custom = NGX_CONF_UNSET_PTR;
    }
    return mlcf;
}

static char *
ngx_http_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_mytest_loc_conf_t *prev = parent;
    ngx_http_mytest_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->flag, prev->flag, 0);
    ngx_conf_merge_str_value(conf->str, prev->str, "");
    ngx_conf_merge_ptr_value(conf->str_array, prev->str_array, NULL);
    ngx_conf_merge_ptr_value(conf->keyval, prev->keyval, NULL);
    ngx_conf_merge_value(conf->num, prev->num, 0);
    ngx_conf_merge_size_value(conf->size, prev->size, 0);
    ngx_conf_merge_off_value(conf->off, prev->off, 0);
    ngx_conf_merge_msec_value(conf->msec, prev->msec, 0);
    ngx_conf_merge_sec_value(conf->sec, prev->sec, 0);
    ngx_conf_merge_bufs_value(conf->bufs, prev->bufs, 8, 4096);
    ngx_conf_merge_uint_value(conf->enum_val, prev->enum_val, 0);
    ngx_conf_merge_bitmask_value(conf->bitmask, prev->bitmask, 0);
    if (ngx_conf_merge_path_value(cf, &conf->path, prev->path, &ngx_http_mytest_path) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }
    ngx_conf_merge_uint_value(conf->access, prev->access, 0600);
    ngx_conf_merge_ptr_value(conf->custom, prev->custom, NULL);

    return NGX_CONF_OK;
}

static char *
ngx_http_mytest_str_post(ngx_conf_t *cf, void *post, void *data)
{
    ngx_str_t  *s = data;
    ngx_uint_t  i = 0;

    for (i = 0; i < s->len; i++) {
        if ((s->data[i] >= 'a' && s->data[i] <= 'z') || (s->data[i] >= 'A' && s->data[i] <= 'Z') || (s->data[i] >= '0' && s->data[i] <= '9') || s->data[i] == '_') {
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character \"%c\" in \"%V\", only [A-Za-z0-9_] is allowed", s->data[i], s);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_mytest_custom(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    u_char                     *p = NULL;
    u_char                     *last = NULL;
    ngx_str_t                  *value = NULL;
    ngx_int_t                   v = 0;
    ngx_uint_t                  i = 0;
    ngx_uint_t                  n = 0;
    ngx_http_mytest_pair_t     *pair = NULL;
    ngx_http_mytest_loc_conf_t *mlcf = conf;

    value = cf->args->elts;
    if (mlcf->custom == NULL || mlcf->custom == NGX_CONF_UNSET_PTR) {
        mlcf->custom = ngx_array_create(cf->pool, 4, sizeof(ngx_http_mytest_pair_t));
        if (mlcf->custom == NULL) {
            return NGX_CONF_ERROR;
        }
    }
    for (i = 1; i < cf->args->nelts; i++) {
        p = value[i].data;
        last = p + value[i].len;
        while (p < last && *p != '=') {
            p++;
        }
        if (p == last || p == value[i].data || p + 1 == last) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid value \"%V\", must be \"key=value\"", &value[i]);
            return NGX_CONF_ERROR;
        }
        pair = ngx_array_push(mlcf->custom);
        if (pair == NULL) {
            return NGX_CONF_ERROR;
        }
        pair->key.data = value[i].data;
        pair->key.len = p - value[i].data;
        for (n = 0; n < pair->key.len; n++) {
            if ((pair->key.data[n] >= 'a' && pair->key.data[n] <= 'z') || pair->key.data[n] == '_') {
                continue;
            }
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid key \"%V\", only [a-z_] is allowed", &pair->key);
            return NGX_CONF_ERROR;
        }
        p++;
        v = ngx_atoi(p, last - p);
        if (v == NGX_ERROR) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid number \"%*s\" in \"%V\"", last - p, p, &value[i]);
            return NGX_CONF_ERROR;
        }
        pair->value = v;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_mytest_dump_handler;

    return NGX_CONF_OK;
}

static size_t
ngx_http_mytest_dump_size(ngx_http_mytest_loc_conf_t *mlcf)
{
    size_t                  size = 0;
    ngx_str_t              *s = NULL;
    ngx_uint_t              i = 0;
    ngx_keyval_t           *kv = NULL;
    ngx_http_mytest_pair_t *pair = NULL;

    size = 2048;
    size += mlcf->str.len;
    if (mlcf->str_array) {
        s = mlcf->str_array->elts;
        for (i = 0; i < mlcf->str_array->nelts; i++) {
            size += s[i].len + 64;
        }
    }
    if (mlcf->keyval) {
        kv = mlcf->keyval->elts;
        for (i = 0; i < mlcf->keyval->nelts; i++) {
            size += kv[i].key.len + kv[i].value.len + 64;
        }
    }
    if (mlcf->custom) {
        pair = mlcf->custom->elts;
        for (i = 0; i < mlcf->custom->nelts; i++) {
            size += pair[i].key.len + 64;
        }
    }
    if (mlcf->path) {
        size += mlcf->path->name.len + 64;
    }

    return size;
}

static ngx_int_t
ngx_http_mytest_dump_handler(ngx_http_request_t *r)
{
    u_char                     *p = NULL;
    u_char                     *last = NULL;
    ngx_str_t                  *s = NULL;
    ngx_int_t                   rc = 0;
    ngx_buf_t                  *b = NULL;
    ngx_uint_t                  i = 0;
    ngx_chain_t                 out;
    ngx_keyval_t               *kv = NULL;
    ngx_conf_enum_t            *e = NULL;
    ngx_conf_bitmask_t         *mask = NULL;
    ngx_http_mytest_pair_t     *pair = NULL;
    ngx_http_mytest_loc_conf_t *mlcf = NULL;
    ngx_memzero(&out, sizeof(ngx_chain_t));

    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }
    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_mytest_module);
    b = ngx_create_temp_buf(r->pool, ngx_http_mytest_dump_size(mlcf));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    p = b->pos;
    last = b->end;
    p = ngx_snprintf(p, last - p, "==== ngx_http_mytest: 内置解析方法演示 ====\n\n");
    p = ngx_snprintf(p, last - p, "1)  ngx_conf_set_flag_slot       mytest_flag        = %s\n", mlcf->flag ? "on" : "off");
    p = ngx_snprintf(p, last - p, "2)  ngx_conf_set_str_slot        mytest_str         = \"%V\"\n", &mlcf->str);
    p = ngx_snprintf(p, last - p, "3)  ngx_conf_set_str_array_slot  mytest_str_array   = ");
    if (mlcf->str_array) {
        s = mlcf->str_array->elts;
        for (i = 0; i < mlcf->str_array->nelts; i++) {
            p = ngx_snprintf(p, last - p, "%s\"%V\"", i ? ", " : "", &s[i]);
        }
    } else {
        p = ngx_snprintf(p, last - p, "(null)");
    }
    p = ngx_snprintf(p, last - p, "\n");
    p = ngx_snprintf(p, last - p, "4)  ngx_conf_set_keyval_slot     mytest_keyval      = ");
    if (mlcf->keyval) {
        kv = mlcf->keyval->elts;
        for (i = 0; i < mlcf->keyval->nelts; i++) {
            p = ngx_snprintf(p, last - p, "%s%V:%V", i ? ", " : "", &kv[i].key, &kv[i].value);
        }
    } else {
        p = ngx_snprintf(p, last - p, "(null)");
    }
    p = ngx_snprintf(p, last - p, "\n");
    p = ngx_snprintf(p, last - p, "5)  ngx_conf_set_num_slot        mytest_num         = %i   (bounds: 1..100)\n", mlcf->num);
    p = ngx_snprintf(p, last - p, "6)  ngx_conf_set_size_slot       mytest_size        = %uz bytes\n", mlcf->size);
    p = ngx_snprintf(p, last - p, "7)  ngx_conf_set_off_slot        mytest_off         = %O bytes\n", mlcf->off);
    p = ngx_snprintf(p, last - p, "8)  ngx_conf_set_msec_slot       mytest_msec        = %M ms\n", mlcf->msec);
    p = ngx_snprintf(p, last - p, "9)  ngx_conf_set_sec_slot        mytest_sec         = %T s\n", mlcf->sec);
    p = ngx_snprintf(p, last - p, "10) ngx_conf_set_bufs_slot       mytest_bufs        = %ui x %uz bytes\n", mlcf->bufs.num, mlcf->bufs.size);
    p = ngx_snprintf(p, last - p, "11) ngx_conf_set_enum_slot       mytest_enum        = %ui", mlcf->enum_val);
    for (e = ngx_http_mytest_enum; e->name.len; e++) {
        if (e->value == mlcf->enum_val) {
            p = ngx_snprintf(p, last - p, " (%V)", &e->name);
            break;
        }
    }
    p = ngx_snprintf(p, last - p, "\n");
    p = ngx_snprintf(p, last - p, "12) ngx_conf_set_bitmask_slot    mytest_bitmask     = 0x%xi", mlcf->bitmask);
    for (mask = ngx_http_mytest_bitmask; mask->name.len; mask++) {
        if (mlcf->bitmask & mask->mask) {
            p = ngx_snprintf(p, last - p, " %V", &mask->name);
        }
    }
    p = ngx_snprintf(p, last - p, "\n");
    p = ngx_snprintf(p, last - p, "13) ngx_conf_set_path_slot       mytest_path        = ");
    if (mlcf->path) {
        p = ngx_snprintf(p, last - p, "%V (level:", &mlcf->path->name);
        for (i = 0; i < NGX_MAX_PATH_LEVEL; i++) {
            if (mlcf->path->level[i] == 0) {
                break;
            }
            p = ngx_snprintf(p, last - p, " %uz", mlcf->path->level[i]);
        }
        p = ngx_snprintf(p, last - p, ")");
    } else {
        p = ngx_snprintf(p, last - p, "(null)");
    }
    p = ngx_snprintf(p, last - p, "\n");
    p = ngx_snprintf(p, last - p, "14) ngx_conf_set_access_slot     mytest_access      = %ui (0x%xi)\n", mlcf->access, mlcf->access);
    p = ngx_snprintf(p, last - p, "15) 自定义 set 回调              mytest_custom      = ");
    if (mlcf->custom) {
        pair = mlcf->custom->elts;
        for (i = 0; i < mlcf->custom->nelts; i++) {
            p = ngx_snprintf(p, last - p, "%s%V=%i", i ? ", " : "", &pair[i].key, pair[i].value);
        }
    } else {
        p = ngx_snprintf(p, last - p, "(null)");
    }
    p = ngx_snprintf(p, last - p, "\n");
    b->last = p;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;
    ngx_str_set(&r->headers_out.content_type, "text/plain; charset=utf-8");
    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}
