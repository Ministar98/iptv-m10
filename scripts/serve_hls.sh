#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

HTTP_PORT="${HTTP_PORT:-8081}"
HTTP_BIND="${HTTP_BIND:-127.0.0.1}"

exec python3 \
    "${PROJECT_ROOT}/scripts/hls_http_server.py" \
    --bind "${HTTP_BIND}" \
    --port "${HTTP_PORT}"
