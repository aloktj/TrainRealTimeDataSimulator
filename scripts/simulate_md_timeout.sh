#!/usr/bin/env bash
set -euo pipefail

BASE_URL=${BASE_URL:-"http://localhost:8848"}
COM_ID=${COM_ID:-2001}
POLL_SECONDS=${POLL_SECONDS:-5}
INTERVAL=${INTERVAL:-1}

echo "Creating MD session for COM ID ${COM_ID}..."
SESSION_JSON=$(curl -sS -X POST "${BASE_URL}/api/md/${COM_ID}/request" -H 'Content-Type: application/json' -d '{}')
SESSION_ID=$(echo "$SESSION_JSON" | jq -r '.sessionId // 0')

if [[ "$SESSION_ID" == "0" ]]; then
  echo "Failed to create session" >&2
  echo "$SESSION_JSON" >&2
  exit 1
fi

echo "Session ${SESSION_ID} created; polling for ${POLL_SECONDS}s to observe reply/timeout transitions"
END_TIME=$(( $(date +%s) + POLL_SECONDS ))
while [[ $(date +%s) -lt ${END_TIME} ]]; do
  STATUS=$(curl -sS "${BASE_URL}/api/md/session/${SESSION_ID}")
  echo "$STATUS" | jq '{state,stats:{txCount,rxCount,retryCount,timeoutCount}}'
  sleep ${INTERVAL}
done
