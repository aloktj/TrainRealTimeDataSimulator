# TrainRealTimeDataSimulator

A lightweight TRDP (Train Real-time Data Protocol) simulator that exercises PD and MD flows, parses TRDP XML configuration, and exposes an HTTP API for controlling telegrams, datasets, and diagnostics.

## Prerequisites

- C++17 toolchain and CMake 3.16+
- Drogon HTTP server, tinyxml2, and nlohmann_json (installed via `apt` in `scripts/setup-trdp-simulator.sh`)
- Optional: a TRDP SDK build that provides `libtrdp.a` and headers (not required when using the built-in stubs)

## Quick start (stubbed TRDP)

```bash
./scripts/setup-trdp-simulator.sh --use-stubs
./scripts/run-local.sh
```

This configures CMake with `TRDP_USE_STUBS=ON`, builds the simulator, and runs it with the default configuration at `config/trdp.xml` on port `8848`.

## Building against a real TRDP library

If you have a TRDP SDK checkout or installation, point the setup script to it:

```bash
./scripts/setup-trdp-simulator.sh --trdp-root /path/to/trdp
# or explicitly
./scripts/setup-trdp-simulator.sh --use-stubs=0 --trdp-lib /path/to/libtrdp.a --trdp-include /path/to/include
```

You can also invoke CMake manually:

```bash
cmake -S . -B build \
  -DTRDP_USE_STUBS=OFF \
  -DTRDP_LIB_PATH=/path/to/libtrdp.a \
  -DTRDP_INCLUDE_DIR=/path/to/trdp/includes
cmake --build build --target trdp-simulator
```

## Running locally

Use the helper to build (if necessary) and launch Drogon:

```bash
./scripts/run-local.sh
```

- The simulator listens on `0.0.0.0:8848` and serves a JSON API.
- Pass `--config path/to/file.xml` to temporarily copy another device XML onto `config/trdp.xml` before launching.
- Use `--use-trdp --trdp-lib ... --trdp-include ...` to run against a proprietary TRDP library instead of the stub.

## Configuration overview

Configuration lives in `config/trdp.xml` and follows the TRDP device schema:

- `<Device>`: host/leader names, device type, memory pools, and debug logging target/level.
- `<ComParameters>`: reusable QoS/TTL definitions referenced by telegrams.
- `<DataSets>`: named dataset schemas (e.g., dataset 100 `TrainStatus` with `REAL32` speed and `BOOL8` doorOpen fields; dataset 101 `Diagnostic` with `UINT32` code and `UINT8` severity).
- `<Interfaces>`: per-network TRDP settings, PD/MD com parameters, and telegrams. PD telegrams define cycle/timeout/validity behavior plus destinations; MD telegrams omit `<PdParameters>` and are treated as message data sessions.
- `<MappedDevices>`: maps COM IDs and host/leader IPs to interface names for redundant or remote peers.

See `config/trdp.xml` for a complete sample that exercises PD publish/subscribe and an MD request telegram.

## HTTP API usage

The Drogon server exposes JSON endpoints:

- PD control: `GET /api/pd/status`, `POST /api/pd/{comId}/enable` with `{ "enabled": true }`.
- Dataset values: `GET /api/datasets/{dataSetId}`; `POST /api/datasets/{dataSetId}/elements/{idx}` with `{ "raw": [0,1,...] }` or `{ "clear": true }`; `POST /api/datasets/{dataSetId}/lock` with `{ "locked": true }`.
- Config: `GET /api/config` and `POST /api/config/reload` with `{ "path": "config/trdp.xml" }`.
- MD: `POST /api/md/{comId}/request` to create/send an MD request, then `GET /api/md/session/{sessionId}` for status.
- Diagnostics: `GET /api/diag/events?max=50`, `GET /api/diag/metrics`, and `POST /api/diag/event` with `{ "component": "sim", "message": "...", "severity": "W" }` to inject events.

## Logging & diagnostics

The simulator honors the `<Debug>` stanza in the XML to route logs to a file (with rotation) or stdout. The new optional `<Pcap>` block (see `config/trdp.xml`) enables binary packet captures with rotation controls and direction filters. CLI flags such as `--pcap-enable`, `--pcap-file <path>`, `--pcap-max-size <bytes>`, `--pcap-max-files <n>`, `--pcap-rx-only`, and `--pcap-tx-only` override the XML at launch.

Diagnostic events are collected by `DiagnosticManager`, which also samples PD/MD metrics and makes them available via `/api/diag/events` and `/api/diag/metrics`.

## Testing

Unit tests are enabled by default when configuring CMake. From the build directory:

```bash
cmake -S . -B build -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=ON
cmake --build build --target trdp-simulator-tests
ctest --test-dir build --output-on-failure
```

Tests are wired into CTest via `gtest_discover_tests`, so `ctest` will pick up any new files under `tests/`. GoogleTest sources are
automatically fetched by CMake; use `ctest -R <pattern>` to run a subset.
