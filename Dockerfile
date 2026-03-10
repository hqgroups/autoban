FROM ubuntu:22.04

# Install necessary dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
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
    && rm -rf /var/lib/apt/lists/*

# Set up environment
ENV container docker
ENV DEBIAN_FRONTEND noninteractive

# Create necessary directories
RUN mkdir -p /etc/autoban /var/run/autoban /app /var/log/nginx

# Copy source code and files
COPY . /app
WORKDIR /app

# Build AutoBan
RUN make clean && make
RUN cp autoban /usr/local/bin/ && chmod +x /usr/local/bin/autoban

# Setup a default nginx config for testing rate limiting
# Inline conf replacement:
RUN echo "limit_req_zone \$binary_remote_addr zone=api_limit:10m rate=50r/m;" > /etc/nginx/conf.d/autoban.conf && \
    echo "set_real_ip_from 0.0.0.0/0;" >> /etc/nginx/conf.d/autoban.conf && \
    echo "real_ip_header X-Real-IP;" >> /etc/nginx/conf.d/autoban.conf && \
    sed -i 's/error_log \/var\/log\/nginx\/error.log;/error_log syslog:server=unix:\/dev\/log,nohostname,tag=nginx_error error;\n    error_log \/var\/log\/nginx\/error.log;/' /etc/nginx/nginx.conf && \
    sed -i '/server_name _;/a \	limit_req zone=api_limit burst=100 nodelay;' /etc/nginx/sites-available/default

# Configure rsyslog to forward to journald
RUN echo 'module(load="omjournal")' > /etc/rsyslog.d/journald.conf && \
    echo '*.* action(type="omjournal")' >> /etc/rsyslog.d/journald.conf

# Setup dummy config for autoban
COPY conf/autoban.conf /etc/autoban/autoban.conf

# Setup a start script
RUN echo '#!/bin/bash\n\
    # Start syslog and journald\n\
    /lib/systemd/systemd-journald &\n\
    rsyslogd\n\
    \n\
    # Initialize ipset\n\
    ipset create autoban_list hash:ip timeout 3600 2>/dev/null || true\n\
    ipset create autoban_list_v6 hash:ip family inet6 timeout 3600 2>/dev/null || true\n\
    \n\
    # Start nginx\n\
    service nginx start\n\
    \n\
    # Start autoban in foreground for debugging\n\
    echo "Starting AutoBan..."\n\
    /usr/local/bin/autoban\n\
    ' > /start.sh && chmod +x /start.sh

# This container needs privileges for iptables/ipset/systemd
ENTRYPOINT ["/start.sh"]
