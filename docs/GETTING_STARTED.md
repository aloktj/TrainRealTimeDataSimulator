# Developer guide & onboarding

This document expands on the README with concrete commands and pointers for new contributors.

## Build flows

### Stubbed TRDP (no proprietary dependencies)

```bash
./scripts/setup-trdp-simulator.sh --use-stubs
```

- Configures CMake with `-DTRDP_USE_STUBS=ON` (the default in CMakeLists).
- Installs Drogon, tinyxml2, and nlohmann_json via `apt` if they are missing.
- Produces `build/trdp-simulator`.

### Linking to a TRDP SDK

```bash
./scripts/setup-trdp-simulator.sh --trdp-root /opt/trdp
# or
./scripts/setup-trdp-simulator.sh --use-stubs=0 --trdp-lib /opt/trdp/lib/libtrdp.a --trdp-include /opt/trdp/include
```

The script auto-detects common TRDP layouts when `--trdp-root` is supplied. You can always pass explicit `--trdp-lib` and `--trdp-include` paths if the heuristic misses your layout.

### Manual CMake

```bash
cmake -S . -B build -DTRDP_USE_STUBS=ON
cmake --build build --target trdp-simulator
```

Add `-DTRDP_ENABLE_TESTS=ON` to configure the GoogleTest targets. Set `-DTRDP_USE_STUBS=OFF` plus TRDP include/lib paths to link against a real SDK.

## Running the simulator locally

```bash
./scripts/run-local.sh
```

- Builds (if needed) and starts Drogon on `0.0.0.0:8848`.
- To test a different XML without committing changes, pass `--config path/to/other.xml`; the script copies it onto `config/trdp.xml` for the run.
- To validate with a vendor TRDP SDK, add `--use-trdp --trdp-lib /path/libtrdp.a --trdp-include /path/include`.

## Configuration anatomy

The sample `config/trdp.xml` demonstrates the supported schema:

- `<Debug>`: enables file logging with a maximum size and severity threshold (used to configure `DiagnosticManager`).
- `<DataSets>`: declares dataset IDs/names and each element's type/array size.
- `<Interfaces>/<Telegrams>`: PD telegrams specify cycle/timeout/validity rules and destinations; MD telegrams omit `<PdParameters>` to indicate MD behavior.
- `<MappedDevices>`: maps COM IDs to host/leader IPs for redundancy or external peers.

See the inline comments and values in the XML for concrete examples of PD publish/subscribe telegrams plus an MD request telegram.

## HTTP API & diagnostics

The Drogon server in `src/main.cpp` registers JSON endpoints:

- PD: `/api/pd/status`, `/api/pd/{comId}/enable`.
- Datasets: `/api/datasets/{id}`, `/api/datasets/{id}/elements/{idx}`, `/api/datasets/{id}/lock`.
- Config: `/api/config`, `/api/config/reload` (accepts `{ "path": "config/trdp.xml" }`).
- MD: `/api/md/{comId}/request` and `/api/md/session/{id}`.
- Diagnostics: `/api/diag/events?max=N`, `/api/diag/metrics`, `/api/diag/event`.

`DiagnosticManager` buffers events, rotates log files when the configured size is exceeded, and periodically samples metrics. The endpoints expose the most recent events and counters so you can verify flows while running tests.

## Test workflow

1. Configure CMake with tests enabled and (optionally) stubs for portability:
   ```bash
   cmake -S . -B build -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=ON
   ```
2. Build the suite:
   ```bash
   cmake --build build --target trdp-simulator-tests
   ```
3. Run the tests from the build directory:
   ```bash
   cd build && ctest --output-on-failure
   ```
4. Filter tests with `ctest -R <regex>` when iterating quickly.

GoogleTest is fetched automatically; you do not need to install it system-wide.
