#!/usr/bin/env bash
set -euo pipefail

BINARY=bin/MiSTer_Zaparoo
REMOTE_PATH=/media/fat/MiSTer_Zaparoo

if [[ -f .env ]]; then
    # shellcheck disable=SC1091
    set -a; source .env; set +a
fi

MISTER_PASS="${MISTER_PASS:-1}"

if [[ -z "${MISTER_IP:-}" ]]; then
    echo "error: MISTER_IP not set — create a .env file with MISTER_IP=<address>" >&2
    exit 1
fi

SSH_OPTS=(-o StrictHostKeyChecking=no -o LogLevel=ERROR)
SCP_OPTS=(-o StrictHostKeyChecking=no -o LogLevel=ERROR)
ssh_cmd() { sshpass -p "$MISTER_PASS" ssh "${SSH_OPTS[@]}" "root@$MISTER_IP" "$@"; }
scp_cmd() { sshpass -p "$MISTER_PASS" scp "${SCP_OPTS[@]}" "$@"; }

echo "==> Building MiSTer_Zaparoo..."
./docker-build.sh

echo "==> Backing up remote binary..."
ssh_cmd "[ -f $REMOTE_PATH ] && mv $REMOTE_PATH ${REMOTE_PATH}.bak || true"

echo "==> Copying new binary..."
scp_cmd "$BINARY" "root@$MISTER_IP:$REMOTE_PATH"

echo "==> Restarting MiSTer_Zaparoo..."
ssh_cmd "killall MiSTer_Zaparoo 2>/dev/null || true; sync; nohup $REMOTE_PATH </dev/null >/dev/ttyS0 2>/dev/ttyS0 &"

echo "==> Done."
