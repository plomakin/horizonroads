#!/usr/bin/env bash
# Publishes a compiled .click revision to OpenStore (open-store.io)
# Usage:
#   OPENSTORE_API_KEY="<your-key>" ./scripts/publish_openstore.sh [changelog]
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
APP_ID="horizonroads.plm"
CLICK_FILE="${PROJ}/release/${APP_ID}_1.5.1_arm64.click"
CHANGELOG="${1:-Initial v1.0.0 release: Endless 3D scenic highway across 6 biomes, dynamic 24h sky & 4 weather modes, 3 cars with bespoke interactive cockpit dashboards & rotating steering wheels, accelerometer tilt steering, Zen Auto-Drive cruise mode, and 3 procedural FM synth radio stations.}"

if [ -z "${OPENSTORE_API_KEY:-}" ]; then
    echo "ERROR: Set OPENSTORE_API_KEY from https://open-store.io/manage (Account -> API Key)." >&2
    exit 1
fi

if [ ! -f "$CLICK_FILE" ]; then
    echo "ERROR: Click file not found at $CLICK_FILE" >&2
    exit 1
fi

CURL_CMD=(curl -4 -fSL)
if command -v devbox >/dev/null 2>&1; then
    CURL_CMD=(devbox curl -4 -fSL)
fi

echo "Uploading $(basename "$CLICK_FILE") to OpenStore (${APP_ID})..."
"${CURL_CMD[@]}" -X POST \
    "https://open-store.io/api/v4/manage/${APP_ID}/revision?apikey=${OPENSTORE_API_KEY}" \
    -F "file=@${CLICK_FILE}" \
    -F "channel=noble" \
    -F "changelog=${CHANGELOG}"
echo
echo "Upload complete! Check https://open-store.io/app/${APP_ID}"
