#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static ngx_int_t ngx_http_mytest_filter_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_mytest_header_filter(ngx_http_request_t *r);
static void ngx_http_mytest_demo_queue(ngx_http_request_t *r);
static void ngx_http_mytest_demo_array(ngx_http_request_t *r);
static void ngx_http_mytest_demo_list(ngx_http_request_t *r);
static void ngx_http_mytest_demo_rbtree(ngx_http_request_t *r);
static void ngx_http_mytest_demo_radix_tree(ngx_http_request_t *r);
static void ngx_http_mytest_demo_hash(ngx_http_request_t *r);
static ngx_int_t ngx_http_mytest_queue_cmp(const ngx_queue_t *one, const ngx_queue_t *two);
static int ngx_libc_cdecl ngx_http_mytest_cmp_dns_wildcards(const void *one, const void *two);
static ngx_int_t ngx_http_mytest_add_key(ngx_hash_keys_arrays_t *ha, u_char *text, void *value);
static void ngx_http_mytest_hash_lookup(ngx_http_request_t *r, ngx_hash_combined_t *hash, u_char *name);
static ngx_http_output_header_filter_pt ngx_http_next_header_filter;

static ngx_command_t ngx_http_mytest_commands[] = {
    ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,                        /* preconfiguration */
    ngx_http_mytest_filter_init, /* postconfiguration */
    NULL,                        /* create main configuration */
    NULL,                        /* init main configuration */
    NULL,                        /* create server configuration */
    NULL,                        /* merge server configuration */
    NULL,                        /* create location configuration */
    NULL                         /* merge location configuration */
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
    return NGX_OK;
}

static ngx_int_t
ngx_http_mytest_header_filter(ngx_http_request_t *r)
{
    if (r == r->main) {
        ngx_http_mytest_demo_queue(r);
        ngx_http_mytest_demo_array(r);
        ngx_http_mytest_demo_list(r);
        ngx_http_mytest_demo_rbtree(r);
        ngx_http_mytest_demo_radix_tree(r);
        ngx_http_mytest_demo_hash(r);
    }
    return ngx_http_next_header_filter(r);
}

typedef struct {
    ngx_queue_t queue;
    ngx_uint_t  num;
} ngx_http_mytest_qnode_t;

static ngx_int_t
ngx_http_mytest_queue_cmp(const ngx_queue_t *one, const ngx_queue_t *two)
{
    ngx_http_mytest_qnode_t *a = ngx_queue_data(one, ngx_http_mytest_qnode_t, queue);
    ngx_http_mytest_qnode_t *b = ngx_queue_data(two, ngx_http_mytest_qnode_t, queue);
    if (a->num < b->num) {
        return -1;
    }
    if (a->num > b->num) {
        return 1;
    }
    return 0;
}

static void
ngx_http_mytest_demo_queue(ngx_http_request_t *r)
{
    ngx_queue_t q;
    ngx_queue_init(&q);
    for (ngx_uint_t i = 0; i < 5; i++) {
        ngx_http_mytest_qnode_t *n = ngx_pcalloc(r->pool, sizeof(ngx_http_mytest_qnode_t));
        if (n == NULL) {
            return;
        }
        n->num = i * 10;
        ngx_queue_insert_tail(&q, &n->queue);
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue: %ui nodes, head -> tail:", q.next == &q ? 0 : 5);
    for (ngx_queue_t *pos = ngx_queue_head(&q); pos != ngx_queue_sentinel(&q); pos = ngx_queue_next(pos)) {
        ngx_http_mytest_qnode_t *n = ngx_queue_data(pos, ngx_http_mytest_qnode_t, queue);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue:   num=%ui", n->num);
    }
    for (ngx_queue_t *pos = ngx_queue_last(&q); pos != ngx_queue_sentinel(&q); pos = ngx_queue_prev(pos)) {
        ngx_http_mytest_qnode_t *n = ngx_queue_data(pos, ngx_http_mytest_qnode_t, queue);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue:   reverse num=%ui", n->num);
    }
    ngx_queue_t *pos = ngx_queue_middle(&q);
    if (pos != ngx_queue_sentinel(&q)) {
        ngx_http_mytest_qnode_t *n = ngx_queue_data(pos, ngx_http_mytest_qnode_t, queue);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue:   middle num=%ui", n->num);
    }
    ngx_queue_sort(&q, ngx_http_mytest_queue_cmp);
    pos = ngx_queue_head(&q);
    ngx_http_mytest_qnode_t *n = ngx_queue_data(pos, ngx_http_mytest_qnode_t, queue);
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue:   after sort, first num=%ui", n->num);
    ngx_queue_remove(ngx_queue_head(&q));
    pos = ngx_queue_head(&q);
    n = ngx_queue_data(pos, ngx_http_mytest_qnode_t, queue);
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue:   after remove head, first num=%ui", n->num);
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queue:   empty=%d", ngx_queue_empty(&q));
}

static void
ngx_http_mytest_demo_array(ngx_http_request_t *r)
{
    ngx_array_t *a = ngx_array_create(r->pool, 4, sizeof(ngx_uint_t));
    if (a == NULL) {
        return;
    }
    for (ngx_uint_t i = 0; i < 5; i++) {
        ngx_uint_t *p = ngx_array_push(a);
        if (p == NULL) {
            return;
        }
        *p = i * i;
    }
    ngx_uint_t *p = ngx_array_push_n(a, 2);
    if (p == NULL) {
        return;
    }
    p[0] = 100;
    p[1] = 200;
    for (ngx_uint_t i = 0; i < a->nelts; i++) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "array:  [%ui] = %ui", i, ((ngx_uint_t *)a->elts)[i]);
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "array:  nelts=%ui nalloc=%ui size=%uz", a->nelts, a->nalloc, a->size);
}

static void
ngx_http_mytest_demo_list(ngx_http_request_t *r)
{
    ngx_list_t *list = ngx_list_create(r->pool, 4, sizeof(ngx_uint_t));
    if (list == NULL) {
        return;
    }
    for (ngx_uint_t i = 0; i < 10; i++) {
        ngx_uint_t *p = ngx_list_push(list);
        if (p == NULL) {
            return;
        }
        *p = i;
    }
    ngx_list_part_t *part = &list->part;
    ngx_uint_t *p = part->elts;
    ngx_uint_t total = 0;
    ngx_uint_t parts = 0;
    for (ngx_uint_t i = 0;; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            p = part->elts;
            i = 0;
            parts++;
        }
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "list:   [%ui] = %ui", total++, p[i]);
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "list:   total=%ui parts=%ui nalloc=%ui", total, parts + 1, list->nalloc);
}

typedef struct {
    ngx_rbtree_node_t node;
    ngx_uint_t        num;
} ngx_http_mytest_rbnode_t;

static void
ngx_http_mytest_demo_rbtree(ngx_http_request_t *r)
{
    ngx_rbtree_node_t *sentinel = ngx_pcalloc(r->pool, sizeof(ngx_rbtree_node_t));
    if (sentinel == NULL) {
        return;
    }
    ngx_rbtree_t tree;
    ngx_rbtree_init(&tree, sentinel, ngx_rbtree_insert_value);
    ngx_uint_t keys[] = { 7, 3, 18, 10, 22, 8, 11, 26, 2, 6 };
    for (ngx_uint_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        ngx_http_mytest_rbnode_t *rn = ngx_pcalloc(r->pool, sizeof(ngx_http_mytest_rbnode_t));
        if (rn == NULL) {
            return;
        }
        rn->num = keys[i] * 100;
        rn->node.key = keys[i];
        ngx_rbtree_insert(&tree, &rn->node);
    }
    ngx_uint_t key = 6;
    ngx_rbtree_node_t *node = tree.root;
    while (node != tree.sentinel) {
        if (key < node->key) {
            node = node->left;
        } else if (key > node->key) {
            node = node->right;
        } else {
            break;
        }
    }

    if (node != tree.sentinel) {
        ngx_http_mytest_rbnode_t *rn = ngx_rbtree_data(node, ngx_http_mytest_rbnode_t, node);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "rbtree: found key=%ui num=%ui", key, rn->num);
    } else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "rbtree: key=%ui not found", key);
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "rbtree: in order:");
    if (tree.root != tree.sentinel) {
        for (node = ngx_rbtree_min(tree.root, tree.sentinel); node != NULL; node = ngx_rbtree_next(&tree, node)) {
            ngx_http_mytest_rbnode_t *rn = ngx_rbtree_data(node, ngx_http_mytest_rbnode_t, node);
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "rbtree:   key=%ui num=%ui", node->key, rn->num);
        }
    }
}

#define MYTEST_IPV4(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (d))

static void
ngx_http_mytest_demo_radix_tree(ngx_http_request_t *r)
{
    ngx_radix_tree_t *tree = ngx_radix_tree_create(r->pool, -1);
    if (tree == NULL) {
        return;
    }
    if (ngx_radix32tree_insert(tree, MYTEST_IPV4(192, 168, 1, 0), 0xffffff00, 1) != NGX_OK || ngx_radix32tree_insert(tree, MYTEST_IPV4(192, 168, 0, 0), 0xffff0000, 2) != NGX_OK || ngx_radix32tree_insert(tree, MYTEST_IPV4(10, 0, 0, 0), 0xff000000, 3) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "radix:  insert failed");
        return;
    }
    uintptr_t value = ngx_radix32tree_find(tree, MYTEST_IPV4(192, 168, 1, 100));
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "radix:  192.168.1.100 -> %ui (192.168.1.0/24)", (ngx_uint_t)value);
    value = ngx_radix32tree_find(tree, MYTEST_IPV4(192, 168, 9, 9));
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "radix:  192.168.9.9   -> %ui (192.168.0.0/16)", (ngx_uint_t)value);
    value = ngx_radix32tree_find(tree, MYTEST_IPV4(172, 16, 0, 1));
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "radix:  172.16.0.1    -> %s", value == NGX_RADIX_NO_VALUE ? "no value" : "found");
    if (ngx_radix32tree_delete(tree, MYTEST_IPV4(192, 168, 1, 0), 0xffffff00) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "radix:  delete failed");
        return;
    }
    value = ngx_radix32tree_find(tree, MYTEST_IPV4(192, 168, 1, 100));
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "radix:  after delete 192.168.1.100 -> %ui (回退到 192.168.0.0/16)", (ngx_uint_t)value);
}

static ngx_str_t ngx_http_mytest_value_exact = ngx_string("exact: www.example.com");
static ngx_str_t ngx_http_mytest_value_head = ngx_string("wc_head: *.example.com");
static ngx_str_t ngx_http_mytest_value_tail = ngx_string("wc_tail: www.example.*");

static ngx_int_t
ngx_http_mytest_add_key(ngx_hash_keys_arrays_t *ha, u_char *text, void *value)
{
    size_t len = ngx_strlen(text);
    ngx_str_t key = ngx_null_string;
    key.len = len;
    key.data = ngx_pnalloc(ha->temp_pool, len);
    if (key.data == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(key.data, text, len);
    return ngx_hash_add_key(ha, &key, value, NGX_HASH_WILDCARD_KEY);
}

static void
ngx_http_mytest_hash_lookup(ngx_http_request_t *r, ngx_hash_combined_t *hash, u_char *name)
{
    size_t len = ngx_strlen(name);
    ngx_str_t *value = ngx_hash_find_combined(hash, ngx_hash_key_lc(name, len), name, len);
    if (value != NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "hash:   \"%s\" -> %V", name, value);
    } else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "hash:   \"%s\" -> (not found)", name);
    }
}

static int ngx_libc_cdecl
ngx_http_mytest_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t *first = (ngx_hash_key_t *)one;
    ngx_hash_key_t *second = (ngx_hash_key_t *)two;
    return ngx_dns_strcmp(first->key.data, second->key.data);
}

static void
ngx_http_mytest_demo_hash(ngx_http_request_t *r)
{
    ngx_pool_t *temp_pool = ngx_create_pool(4096, r->connection->log);
    if (temp_pool == NULL) {
        return;
    }
    ngx_hash_keys_arrays_t ha;
    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
    ha.temp_pool = temp_pool;
    ha.pool = r->pool;
    if (ngx_hash_keys_array_init(&ha, NGX_HASH_SMALL) != NGX_OK) {
        goto failed;
    }
    if (ngx_http_mytest_add_key(&ha, (u_char *)"www.example.com", &ngx_http_mytest_value_exact) != NGX_OK || ngx_http_mytest_add_key(&ha, (u_char *)"static.example.com", &ngx_http_mytest_value_exact) != NGX_OK) {
        goto failed;
    }
    if (ngx_http_mytest_add_key(&ha, (u_char *)"*.example.com", &ngx_http_mytest_value_head) != NGX_OK) {
        goto failed;
    }
    if (ngx_http_mytest_add_key(&ha, (u_char *)"www.example.*", &ngx_http_mytest_value_tail) != NGX_OK) {
        goto failed;
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "hash:   keys=%ui wc_head=%ui wc_tail=%ui", ha.keys.nelts, ha.dns_wc_head.nelts, ha.dns_wc_tail.nelts);
    ngx_hash_init_t hash_init;
    ngx_hash_t hash;
    hash_init.hash = &hash;
    hash_init.key = &ngx_hash_key_lc;
    hash_init.max_size = 512;
    hash_init.bucket_size = 64;
    hash_init.name = "mytest_hash";
    hash_init.pool = r->pool;
    hash_init.temp_pool = temp_pool;
    if (ha.keys.nelts && ngx_hash_init(&hash_init, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
        goto failed;
    }
    ngx_hash_wildcard_t *wc_head = NULL;
    ngx_hash_wildcard_t *wc_tail = NULL;
    if (ha.dns_wc_head.nelts) {
        ngx_qsort(ha.dns_wc_head.elts, (size_t)ha.dns_wc_head.nelts, sizeof(ngx_hash_key_t), ngx_http_mytest_cmp_dns_wildcards);
        hash_init.hash = NULL;
        hash_init.name = "mytest_hash_wc_head";
        if (ngx_hash_wildcard_init(&hash_init, ha.dns_wc_head.elts, ha.dns_wc_head.nelts) != NGX_OK) {
            goto failed;
        }
        wc_head = (ngx_hash_wildcard_t *)hash_init.hash;
    }
    if (ha.dns_wc_tail.nelts) {
        ngx_qsort(ha.dns_wc_tail.elts, (size_t)ha.dns_wc_tail.nelts, sizeof(ngx_hash_key_t), ngx_http_mytest_cmp_dns_wildcards);
        hash_init.hash = NULL;
        hash_init.name = "mytest_hash_wc_tail";
        if (ngx_hash_wildcard_init(&hash_init, ha.dns_wc_tail.elts, ha.dns_wc_tail.nelts) != NGX_OK) {
            goto failed;
        }
        wc_tail = (ngx_hash_wildcard_t *)hash_init.hash;
    }
    ngx_hash_combined_t combined;
    combined.hash = hash;
    combined.wc_head = wc_head;
    combined.wc_tail = wc_tail;

    ngx_http_mytest_hash_lookup(r, &combined, (u_char *)"www.example.com");
    ngx_http_mytest_hash_lookup(r, &combined, (u_char *)"static.example.com");
    ngx_http_mytest_hash_lookup(r, &combined, (u_char *)"a.b.example.com");
    ngx_http_mytest_hash_lookup(r, &combined, (u_char *)"www.example.com.cn");
    ngx_http_mytest_hash_lookup(r, &combined, (u_char *)"www.other.com");
    ngx_destroy_pool(temp_pool);
    return;
failed:
    ngx_destroy_pool(temp_pool);
}
