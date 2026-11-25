#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DESTINATION_IP="${RTP_IP:-127.0.0.1}"
DESTINATION_PORT="${RTP_PORT:-5004}"

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <channel_id>"
    echo "Available channels: 1, 2, 3"
    exit 1
fi

CHANNEL_ID="$1"

case "${CHANNEL_ID}" in
    1|2|3)
        ;;
    *)
        echo "ERROR: Invalid channel ID: ${CHANNEL_ID}"
        exit 1
        ;;
esac

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ERROR: FFmpeg is not installed."
    exit 1
fi

MEDIA_FILE="${PROJECT_ROOT}/media/channel${CHANNEL_ID}.ts"

if [[ ! -f "${MEDIA_FILE}" ]]; then
    echo "ERROR: Media file not found: ${MEDIA_FILE}"
    echo "Run scripts/generate_media.sh first."
    exit 1
fi

echo "========================================"
echo "IPTV M10 Headend"
echo "Channel: ${CHANNEL_ID}"
echo "Media: ${MEDIA_FILE}"
echo "Protocol: MPEG-TS over RTP"
echo "Destination: ${DESTINATION_IP}:${DESTINATION_PORT}"
echo "========================================"

exec ffmpeg \
    -hide_banner \
    -loglevel warning \
    -re \
    -stream_loop -1 \
    -i "${MEDIA_FILE}" \
    -map 0:v:0 \
    -map 0:a:0 \
    -c copy \
    -mpegts_muxer_options "mpegts_service_id=${CHANNEL_ID}" \
    -f rtp_mpegts \
    "rtp://${DESTINATION_IP}:${DESTINATION_PORT}?pkt_size=1328"
