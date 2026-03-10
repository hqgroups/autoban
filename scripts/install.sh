#!/usr/bin/env bash
# ==============================================================================
# AutoBan — Universal Install Script
# Supports: Ubuntu 18.04, 20.04, 22.04, 24.04 | Arch: x86_64, i386, aarch64
# Usage:
#   curl -sSL https://raw.../install.sh | sudo bash
#   sudo bash install.sh [--arch i386] [--no-nginx]
# ==============================================================================
set -euo pipefail

AUTOBAN_CONF_DIR="/etc/autoban"
AUTOBAN_RUN_DIR="/var/run/autoban"
INSTALL_BIN="/usr/local/bin/autoban"

# ── Parse arguments ────────────────────────────────────────────────────────────
FORCE_ARCH=""
SKIP_NGINX=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) FORCE_ARCH="$2"; shift 2 ;;
        --no-nginx) SKIP_NGINX=1; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# ── Detect environment ─────────────────────────────────────────────────────────
ARCH="${FORCE_ARCH:-$(uname -m)}"
UBUNTU_VER=$(lsb_release -rs 2>/dev/null || echo "unknown")
echo "▶ AutoBan installer | Ubuntu ${UBUNTU_VER} | arch=${ARCH}"

# ── Check root ─────────────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    echo "ERROR: Run as root (sudo)"
    exit 1
fi

# ── Install dependencies ───────────────────────────────────────────────────────
echo "▶ Installing dependencies..."
apt-get update -qq

PKGS=(
    build-essential
    pkg-config
    libsystemd-dev
    libhiredis-dev
    ipset
    iptables
    curl
)

if [[ "$SKIP_NGINX" -eq 0 ]]; then
    PKGS+=(nginx)
fi

# 32-bit cross-compile support
if [[ "$ARCH" == "i386" || "$ARCH" == "i686" ]]; then
    echo "▶ Enabling 32-bit multilib support..."
    dpkg --add-architecture i386
    apt-get update -qq
    PKGS+=(gcc-multilib libsystemd-dev:i386 libhiredis-dev:i386)
fi

apt-get install -y --no-install-recommends "${PKGS[@]}"
echo "✓ Dependencies installed."

# ── Build ──────────────────────────────────────────────────────────────────────
echo "▶ Building AutoBan (arch=${ARCH})..."

# Pass ARCH explicitly to Makefile
MAKE_ARGS="ARCH=${ARCH}"

make clean
make ${MAKE_ARGS}
echo "✓ Build successful."

# ── Install binary ─────────────────────────────────────────────────────────────
install -m 755 autoban "${INSTALL_BIN}"
echo "✓ Installed: ${INSTALL_BIN}"

# ── Setup User and Permissions ────────────────────────────────────────────────
echo "▶ Setting up autoban user..."
id autoban >/dev/null 2>&1 || useradd -r -s /bin/false autoban
echo "✓ User 'autoban' ready."

# ── Setup directories and config ───────────────────────────────────────────────
mkdir -p "${AUTOBAN_CONF_DIR}" "${AUTOBAN_RUN_DIR}"

if [[ ! -f "${AUTOBAN_CONF_DIR}/autoban.conf" ]]; then
    install -m 600 -o autoban -g autoban conf/autoban.conf "${AUTOBAN_CONF_DIR}/autoban.conf"
    echo "✓ Config installed: ${AUTOBAN_CONF_DIR}/autoban.conf"
else
    chown autoban:autoban "${AUTOBAN_CONF_DIR}/autoban.conf"
    chmod 600 "${AUTOBAN_CONF_DIR}/autoban.conf"
    echo "⚠ Config exists — updated permissions."
fi
chown autoban:autoban "${AUTOBAN_RUN_DIR}"
chmod 755 "${AUTOBAN_RUN_DIR}"

# ── Setup ipset ────────────────────────────────────────────────────────────────
echo "▶ Initializing ipset..."
ipset create autoban_list hash:ip timeout 3600 2>/dev/null || true
ipset create autoban_list_v6 hash:ip family inet6 timeout 3600 2>/dev/null || true
echo "✓ ipset ready."

# ── Install systemd service ────────────────────────────────────────────────────
if [[ -f systemd/autoban.service ]]; then
    install -m 644 systemd/autoban.service /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable autoban.service
    echo "✓ systemd service installed and enabled."
fi

# ── Done ───────────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════"
echo "  AutoBan installed successfully!"
echo "  Ubuntu : ${UBUNTU_VER}"
echo "  Arch   : ${ARCH}"
echo "  Binary : ${INSTALL_BIN}"
echo "  Config : ${AUTOBAN_CONF_DIR}/autoban.conf"
echo ""
echo "  Start : systemctl start autoban"
echo "  Status: systemctl status autoban"
echo "═══════════════════════════════════════"
