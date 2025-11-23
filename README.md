# TrainRealTimeDataSimulator

[![CI](https://github.com/TrainRealTimeDataSimulator/TrainRealTimeDataSimulator/actions/workflows/ci.yml/badge.svg)](https://github.com/TrainRealTimeDataSimulator/TrainRealTimeDataSimulator/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A lightweight TRDP (Train Real-time Data Protocol) simulator that exercises PD and MD flows, parses TRDP XML configuration, and exposes an HTTP API for controlling telegrams, datasets, and diagnostics.

> Note: Clone this repository with submodules so the optional [TCNopen](https://github.com/aloktj/TCNopen.git) dependency is available for builds against an open-source TRDP stack:
>
> ```bash
> git clone --recursive https://github.com/TrainRealTimeDataSimulator/TrainRealTimeDataSimulator.git
> # or, if already cloned
> git submodule update --init --recursive
> ```

## Prerequisites

- C++17 toolchain and CMake 3.16+
- Drogon HTTP server, tinyxml2, and nlohmann_json (installed via `apt` in `scripts/setup-trdp-simulator.sh`)
- Optional: a TRDP SDK build that provides `libtrdp.a` and headers (not required when using the built-in stubs)

## Quick start (stubbed TRDP)

```bash
./scripts/setup-trdp-simulator.sh --use-stubs
./scripts/run-local.sh
```

This configures CMake with `TRDP_USE_STUBS=ON`, builds the simulator, and runs it with the default configuration at `config/trdp.xml` on port `8000`.

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

### Using the TCNopen submodule for TRDP

To build against the open-source TRDP implementation in the `external/TCNopen` submodule, enable the dedicated CMake switch and disable the built-in stubs:

```bash
git submodule update --init --recursive
cmake -S . -B build \
  -DTRDP_USE_STUBS=OFF \
  -DTRDP_USE_TCNOPEN=ON
cmake --build build --target trdp-simulator
```

You can also let the helper script wire this up by passing `--use-tcnopen`:

```bash
./scripts/setup-trdp-simulator.sh --use-tcnopen
```

When running against the real stack, make sure the device XML references an IP address/nic pair that exists on your system. The
default `config/trdp.xml` now binds to loopback (`127.0.0.1`/`lo`) so the TRDP sockets can open cleanly without extra network
setup.

Configuration will fail early if the submodule is missing while `TRDP_USE_TCNOPEN` is enabled.

## Running locally

Use the helper to build (if necessary) and launch Drogon:

```bash
./scripts/run-local.sh
```

- The simulator listens on `0.0.0.0:8000` and serves a JSON API.
- Pass `--config path/to/file.xml` to temporarily copy another device XML onto `config/trdp.xml` before launching.
- Use `--use-trdp --trdp-lib ... --trdp-include ...` to run against a proprietary TRDP library instead of the stub.

## Configuration overview

Configuration lives in `config/trdp.xml` and follows the TRDP device schema:

- `<Device>`: host/leader names, device type, memory pools, and debug logging target/level.
- `<ComParameters>`: reusable QoS/TTL definitions referenced by telegrams.
- `<DataSets>`: named dataset schemas (e.g., dataset 100 `TrainStatus` with `REAL32` speed and `BOOL8` doorOpen fields; dataset 101 `Diagnostic` with `UINT32` code and `UINT8` severity).
- `<Interfaces>`: per-network TRDP settings, PD/MD com parameters, and telegrams. PD telegrams define cycle/timeout/validity behavior plus destinations; MD telegrams omit `<PdParameters>` and are treated as message data sessions.
- `<MulticastGroups>` (inside each `<Interface>`): multicast addresses (and optional `nic` overrides) that are joined automatically at startup, useful for unicast/multicast routing across multiple NICs.
- `<MappedDevices>`: maps COM IDs and host/leader IPs to interface names for redundant or remote peers.

See `config/trdp.xml` for a complete sample that exercises PD publish/subscribe and an MD request telegram.

## HTTP API usage

The Drogon server exposes JSON endpoints:

- PD control: `GET /api/pd/status`, `POST /api/pd/{comId}/enable` with `{ "enabled": true }`.
- Dataset values: `GET /api/datasets/{dataSetId}`; `POST /api/datasets/{dataSetId}/elements/{idx}` with `{ "raw": [0,1,...] }` or `{ "clear": true }`; `POST /api/datasets/{dataSetId}/lock` with `{ "locked": true }`.
- Config: `GET /api/config` and `POST /api/config/reload` with `{ "path": "config/trdp.xml" }`.
- Multicast: `GET /api/network/multicast` for current membership; `POST /api/network/multicast/join` or `/leave` with `{ "interface": "eth0", "group": "239.0.0.1", "nic": "br0" }` to manually manage joins.
- MD: `POST /api/md/{comId}/request` to create/send an MD request, then `GET /api/md/session/{sessionId}` for status.
- Diagnostics: `GET /api/diag/events?max=50`, `GET /api/diag/metrics`, and `POST /api/diag/event` with `{ "component": "sim", "message": "...", "severity": "W" }` to inject events.

## Logging & diagnostics

The simulator honors the `<Debug>` stanza in the XML to route logs to a file (with rotation) or stdout. The new optional `<Pcap>` block (see `config/trdp.xml`) enables binary packet captures with rotation controls and direction filters. CLI flags such as `--pcap-enable`, `--pcap-file <path>`, `--pcap-max-size <bytes>`, `--pcap-max-files <n>`, `--pcap-rx-only`, and `--pcap-tx-only` override the XML at launch.

Diagnostic events are collected by `DiagnosticManager`, which also samples PD/MD metrics and makes them available via `/api/diag/events` and `/api/diag/metrics`.

## Scripting hooks

Use `scripts/scripting_hooks.py` to drive PD/MD flows from JSON, Python, or Lua scripts and to capture automated pass/fail reports alongside exported diagnostic logs. See [docs/Scripting.md](docs/Scripting.md) for examples.

## Testing

Unit tests are enabled by default when configuring CMake. From the build directory:

```bash
cmake -S . -B build -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=ON
cmake --build build --target trdp-simulator-tests
ctest --test-dir build --output-on-failure
```

Tests are wired into CTest via `gtest_discover_tests`, so `ctest` will pick up any new files under `tests/`. GoogleTest sources are
automatically fetched by CMake; use `ctest -R <pattern>` to run a subset.

## Deployment artifacts

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for end-to-end packaging steps:

- Generate a `.deb` with `cmake --build build --target package` (uses CPack and installs the systemd unit plus defaults).
- Build a multi-stage Docker image (`docker build`) or a Pi-targeted image (`docker buildx build --platform linux/arm64/v8`).
- Install/run the packaged systemd service and tune runtime flags in `/etc/default/trdp-simulator`.

## Contributing

We welcome contributions! A few quick guidelines:

- Keep code formatted with `clang-format` (configuration in `.clang-format`). You can run `clang-format -i $(find src include tests -name '*.cpp' -o -name '*.hpp')` before committing.
- Use stubbed TRDP for CI-friendly builds: `cmake -S . -B build -GNinja -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=ON -DTRDP_WARNINGS_AS_ERRORS=ON`.
- Run the test suite via `ctest --test-dir build --output-on-failure` before opening a pull request.
- Include context in PR descriptions (what changed, why, and any testing performed).
