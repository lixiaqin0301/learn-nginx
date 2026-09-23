#!/bin/bash
set -euo pipefail
sh_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)
cd "$sh_dir"
git clean -ffdx
grep -q nginx /etc/group || groupadd -r nginx
grep -q nginx /etc/passwd || useradd -g nginx -M -r nginx
./auto/configure --prefix="$sh_dir"/prefix \
  --with-http_v2_module             \
  --with-http_v3_module             \
  --with-http_realip_module         \
  --with-http_addition_module       \
  --with-http_xslt_module           \
  --with-http_image_filter_module   \
  --with-http_geoip_module          \
  --with-http_sub_module            \
  --with-http_dav_module            \
  --with-http_flv_module            \
  --with-http_mp4_module            \
  --with-http_gunzip_module         \
  --with-http_gzip_static_module    \
  --with-http_auth_request_module   \
  --with-http_random_index_module   \
  --with-http_secure_link_module    \
  --with-http_degradation_module    \
  --with-http_slice_module          \
  --with-http_json_module           \
  --with-http_stub_status_module    \
  --with-cc-opt="-g -O0"            \
  --with-debug
bear -- make -s -j"$(nproc)"
make install
python3 clangd-index.py
while killall -q nginx; do
    sleep 1
done
while killall -q openresty; do
    sleep 1
done
cat > prefix/conf/nginx.conf << 'EOF'
user nginx;
events { }
http {
    include       mime.types;
    default_type  application/octet-stream;
    upstream backend {
        server   127.0.0.82:9082;
        server   127.0.0.83:9083;
        server   127.0.0.84:9084;
    }
    server {
        listen   127.0.0.1:8081;
        location / {
            proxy_pass  http://backend;
        }

    }
}
EOF
export PATH="$sh_dir/prefix/sbin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin"
chown -R nginx:nginx prefix
chown root:root prefix/sbin/nginx
nginx -p "$sh_dir/prefix"
mkdir openresty/logs
chown -R openresty:openresty openresty
openresty -p "$sh_dir/openresty"
sleep 1
ps u -C nginx,openresty
