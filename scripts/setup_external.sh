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

ensure_jsoncpp() {
  if pkg-config --exists jsoncpp; then
    return
  fi

  echo "[setup_external] jsoncpp development package not detected; installing via apt..."
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y >/dev/null
  apt-get install -y --no-install-recommends libjsoncpp-dev >/dev/null
}

ensure_nlohmann_json() {
  if [[ -f "/usr/include/nlohmann/json.hpp" ]]; then
    return
  fi

  echo "[setup_external] nlohmann_json headers not detected; installing via apt..."
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y >/dev/null
  apt-get install -y --no-install-recommends nlohmann-json3-dev >/dev/null
}

ensure_trantor() {
  local drogon_dir="$1"
  local trantor_dir="${drogon_dir}/trantor"
  if [[ -f "${trantor_dir}/CMakeLists.txt" ]]; then
    return
  fi

  echo "[setup_external] Drogon archive missing trantor; fetching upstream release..."
  rm -rf "${drogon_dir}"
  git clone --branch v1.9.11 --recurse-submodules https://github.com/drogonframework/drogon.git "${drogon_dir}" >/dev/null
}

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

# Drogon archive packaged with the repo doesn't include its trantor submodule.
# Fetch it explicitly when missing to keep the build reproducible.
ensure_trantor "${EXTERNAL_DIR}/drogon"

# Drogon requires jsoncpp; install it if it's not already present.
ensure_jsoncpp

# The simulator uses nlohmann_json headers when the system package is available.
ensure_nlohmann_json

echo
echo "[setup_external] All external dependencies are ready."
