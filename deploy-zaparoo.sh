#!/usr/bin/env bash
set -euo pipefail

BINARY=bin/MiSTer_Zaparoo
REMOTE_DIR=/media/fat/zaparoo
REMOTE_PATH=$REMOTE_DIR/MiSTer_Zaparoo
STABLE_BUILD=0

usage() {
    echo "usage: $0 [--stable]" >&2
    echo "  --stable  build from the latest upstream stable release before deploying" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stable)
            STABLE_BUILD=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
    shift
done

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
if [[ "$STABLE_BUILD" = "1" ]]; then
    ./stable-build.sh
else
    ./docker-build.sh
fi

echo "==> Backing up remote binary..."
ssh_cmd "mkdir -p $REMOTE_DIR; [ -f $REMOTE_PATH ] && mv $REMOTE_PATH ${REMOTE_PATH}.bak || true"

echo "==> Copying new binary..."
scp_cmd "$BINARY" "root@$MISTER_IP:$REMOTE_PATH"

echo "==> Restarting MiSTer_Zaparoo..."
ssh_cmd "killall MiSTer_Zaparoo 2>/dev/null || true; sync; nohup $REMOTE_PATH </dev/null >/dev/ttyS0 2>/dev/ttyS0 &"

echo "==> Done."
