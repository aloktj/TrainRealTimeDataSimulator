#!/usr/bin/env bash
set -euo pipefail

BASE_URL=${BASE_URL:-"http://localhost:8000"}
COM_ID=${COM_ID:-1001}
DATASET_ID=${DATASET_ID:-1}

# Push a couple of data updates, read them back, and toggle enable state.
echo "Writing PD dataset ${DATASET_ID} for COM ID ${COM_ID}..."
curl -sS -X POST "${BASE_URL}/api/datasets/${DATASET_ID}/elements/0" \
    -H 'Content-Type: application/json' \
    -d '{"raw":[1,2]}' > /dev/null

curl -sS -X POST "${BASE_URL}/api/datasets/${DATASET_ID}/elements/1" \
    -H 'Content-Type: application/json' \
    -d '{"raw":[1]}' > /dev/null

echo "Current PD dataset snapshot:"
curl -sS "${BASE_URL}/api/datasets/${DATASET_ID}" | jq .

echo "Disabling COM ID ${COM_ID} for 2 seconds to simulate a pause..."
curl -sS -X POST "${BASE_URL}/api/pd/${COM_ID}/enable" \
    -H 'Content-Type: application/json' \
    -d '{"enabled":false}' > /dev/null
sleep 2

echo "Re-enabling COM ID ${COM_ID}"
curl -sS -X POST "${BASE_URL}/api/pd/${COM_ID}/enable" \
    -H 'Content-Type: application/json' \
    -d '{"enabled":true}' > /dev/null

echo "Final PD status:"
curl -sS "${BASE_URL}/api/pd/status" | jq .
