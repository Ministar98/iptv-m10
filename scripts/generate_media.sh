#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MEDIA_DIR="${PROJECT_ROOT}/media"

DURATION=120
RESOLUTION="1280x720"
FPS=25
GOP=50

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ERROR: FFmpeg is not installed."
    exit 1
fi

mkdir -p "${MEDIA_DIR}"

generate_channel()
{
    local channel_id="$1"
    local video_source="$2"
    local audio_frequency="$3"

    local output_file="${MEDIA_DIR}/channel${channel_id}.ts"

    echo "Generating channel ${channel_id}..."
    echo "Output: ${output_file}"

    ffmpeg \
        -hide_banner \
        -loglevel warning \
        -y \
        -f lavfi \
        -i "${video_source}=size=${RESOLUTION}:rate=${FPS}" \
        -f lavfi \
        -i "sine=frequency=${audio_frequency}:sample_rate=48000" \
        -t "${DURATION}" \
        -c:v libx264 \
        -preset ultrafast \
        -threads 2 \
        -pix_fmt yuv420p \
        -b:v 2000k \
        -maxrate 2000k \
        -bufsize 4000k \
        -g "${GOP}" \
        -keyint_min "${GOP}" \
        -sc_threshold 0 \
        -flags +cgop \
        -c:a aac \
        -b:a 128k \
        -ar 48000 \
        -ac 2 \
        -mpegts_service_id "${channel_id}" \
        -metadata service_provider="IPTV M10" \
        -metadata service_name="Channel ${channel_id}" \
        -f mpegts \
        "${output_file}"

    echo "Channel ${channel_id} generated successfully."
    echo
}

generate_channel 1 "testsrc2" 440
generate_channel 2 "smptebars" 550
generate_channel 3 "rgbtestsrc" 660

echo "All IPTV test channels generated successfully."
