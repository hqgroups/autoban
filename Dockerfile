# AutoBan — Multi-arch, multi-Ubuntu Dockerfile
# Supports: Ubuntu 18.04, 20.04, 22.04, 24.04
# Platforms: linux/amd64, linux/arm64
#
# Build for current platform:
#   docker build -t autoban .
#
# Build multi-arch with buildx:
#   docker buildx build --platform linux/amd64,linux/arm64 -t autoban:latest --push .

# ── Use ARG so base image can be overridden ────────────────────────────────────
ARG UBUNTU_VERSION=22.04
FROM ubuntu:${UBUNTU_VERSION}

# Re-declare to use inside stages
ARG UBUNTU_VERSION=22.04

LABEL maintainer="AutoBan"
LABEL description="AutoBan IP Blocker — ubuntu:${UBUNTU_VERSION}"

# ── Avoid interactive prompts ──────────────────────────────────────────────────
ENV DEBIAN_FRONTEND=noninteractive
ENV container=docker

# ── Install dependencies (compatible across 18.04 → 24.04) ───────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    pkg-config \
    libsystemd-dev \
    libhiredis-dev \
    ipset \
    iptables \
    nginx \
    curl \
    socat \
    iproute2 \
    systemd \
    systemd-sysv \
    rsyslog \
    lsb-release \
    && rm -rf /var/lib/apt/lists/*

# ── Install 32-bit libs if building on x86_64 for i386 ────────────────────────
# Uncomment this block if cross-compiling to 32-bit:
# RUN dpkg --add-architecture i386 && apt-get update && \
#     apt-get install -y gcc-multilib libsystemd-dev:i386 libhiredis-dev:i386 && \
#     rm -rf /var/lib/apt/lists/*

# ── Create required directories ────────────────────────────────────────────────
RUN mkdir -p /etc/autoban /var/run/autoban /app /var/log/nginx

# ── Copy source and build ──────────────────────────────────────────────────────
COPY . /app
WORKDIR /app

# Let Makefile auto-detect arch and Ubuntu via pkg-config
RUN make clean && make
RUN cp autoban /usr/local/bin/ && chmod +x /usr/local/bin/autoban

# ── Nginx rate limiting config ─────────────────────────────────────────────────
RUN echo "limit_req_zone \$binary_remote_addr zone=api_limit:10m rate=50r/m;" > /etc/nginx/conf.d/autoban.conf && \
    echo "set_real_ip_from 0.0.0.0/0;"  >> /etc/nginx/conf.d/autoban.conf && \
    echo "real_ip_header X-Real-IP;"    >> /etc/nginx/conf.d/autoban.conf && \
    sed -i 's/error_log \/var\/log\/nginx\/error.log;/error_log syslog:server=unix:\/dev\/log,nohostname,tag=nginx_error error;\n    error_log \/var\/log\/nginx\/error.log;/' /etc/nginx/nginx.conf && \
    sed -i '/server_name _;/a \\tlimit_req zone=api_limit burst=100 nodelay;' /etc/nginx/sites-available/default

# ── rsyslog → journald forwarding ─────────────────────────────────────────────
RUN echo 'module(load="omjournal")' > /etc/rsyslog.d/journald.conf && \
    echo '*.* action(type="omjournal")' >> /etc/rsyslog.d/journald.conf

# ── AutoBan config ─────────────────────────────────────────────────────────────
COPY conf/autoban.conf /etc/autoban/autoban.conf

# ── Start script ───────────────────────────────────────────────────────────────
RUN printf '#!/bin/bash\n\
    set -e\n\
    /lib/systemd/systemd-journald &\n\
    rsyslogd\n\
    \n\
    ipset create autoban_list hash:ip timeout 3600 2>/dev/null || true\n\
    ipset create autoban_list_v6 hash:ip family inet6 timeout 3600 2>/dev/null || true\n\
    \n\
    service nginx start\n\
    \n\
    echo "AutoBan $(autoban --version 2>/dev/null || echo dev) starting..."\n\
    exec /usr/local/bin/autoban\n\
    ' > /start.sh && chmod +x /start.sh

ENTRYPOINT ["/start.sh"]
