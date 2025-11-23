#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTERNAL_DIR="${ROOT_DIR}/external"

# List of deps: name => zip file
declare -A DEPS=(
  ["TCNopen"]="TCNopen.zip"
  ["drogon"]="drogon.zip"
  ["tinyxml2"]="tinyxml2.zip"
)

echo "[setup_external] Project root: ${ROOT_DIR}"
echo "[setup_external] External dir: ${EXTERNAL_DIR}"
mkdir -p "${EXTERNAL_DIR}"

for NAME in "${!DEPS[@]}"; do
  ZIP_NAME="${DEPS[$NAME]}"
  ZIP_PATH="${EXTERNAL_DIR}/${ZIP_NAME}"
  TARGET_DIR="${EXTERNAL_DIR}/${NAME}"

  echo
  echo "=== ${NAME} ==="
  echo "Zip:    ${ZIP_PATH}"
  echo "Target: ${TARGET_DIR}"

  if [[ ! -f "${ZIP_PATH}" ]]; then
    echo "ERROR: Zip file '${ZIP_PATH}' not found."
    echo "       Make sure '${ZIP_NAME}' is committed under 'external/'."
    exit 1
  fi

  if [[ -d "${TARGET_DIR}" ]] && [[ -n "$(ls -A "${TARGET_DIR}" 2>/dev/null || true)" ]]; then
    echo "Already extracted: ${TARGET_DIR} (skipping unzip)"
    continue
  fi

  echo "Extracting ${ZIP_NAME} into ${TARGET_DIR}..."
  mkdir -p "${TARGET_DIR}"
  unzip -q "${ZIP_PATH}" -d "${TARGET_DIR}"
  echo "Done: ${NAME}"
done

echo
echo "[setup_external] All external dependencies are ready."
