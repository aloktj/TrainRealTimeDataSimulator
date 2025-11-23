# Deployment, packaging, and runtime configuration

This guide describes how to build deployable artifacts (.deb, Docker image, systemd unit) and how to configure the simulator on both VM and Raspberry Pi targets.

## Build artifacts

### Debian package (.deb)
1. Configure a build directory:
   ```bash
   cmake -S . -B build -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=OFF
   ```
2. Compile and generate the package (runs CPack):
   ```bash
   cmake --build build --target package
   # Result: build/trdp-simulator-<version>-linux.deb
   ```
3. Install on a VM or Pi:
   ```bash
   sudo apt install ./build/trdp-simulator-*.deb
   ```
4. For teams preferring `dpkg-buildpackage`, the `packaging/debian` folder ships
   `control`, `changelog`, and `.install` manifests that mirror the CPack output.
   Point `debuild` at the repository root after running `cmake --build`.

### Docker image
- Build for the host architecture:
  ```bash
  docker build -t trdp-simulator:latest .
  ```
- Build for Raspberry Pi from an x86 host (requires Docker Buildx):
  ```bash
  docker buildx build --platform linux/arm64/v8 -t trdp-simulator:pi .
  ```
- Use multi-arch build arguments (the Dockerfile sets `TRDP_PI_OPTIMIZED=ON` when
  targeting arm/arm64):
  ```bash
  docker buildx build --platform linux/arm/v7,linux/arm64/v8,linux/amd64 -t trdp-simulator:multi .
  ```
- Run the image (bind a config file and expose the API):
  ```bash
  docker run --rm -p 8000:8000 \
    -v $(pwd)/config/trdp.xml:/etc/trdp-simulator/trdp.xml:ro \
    trdp-simulator:latest
  ```

### Systemd unit
1. Install the `.deb` or manually copy files:
   ```bash
   sudo install -m 644 config/systemd/trdp-simulator.service /lib/systemd/system/
   sudo install -m 644 config/trdp.xml /etc/trdp-simulator/trdp.xml
   sudo install -m 644 config/trdp-simulator.env /etc/default/trdp-simulator
   sudo systemctl daemon-reload
   sudo systemctl enable --now trdp-simulator.service
   ```
2. Override runtime options in `/etc/default/trdp-simulator` (see Runtime configuration below).

## Runtime configuration

### VM profile (x86_64)
- Keep defaults from `config/trdp.xml` or replace with a site-specific TRDP device XML.
- Adjust logging/PCAP overrides by editing `/etc/default/trdp-simulator`, e.g.:
  ```bash
  TRDP_SIMULATOR_OPTS="--pcap-enable --pcap-file /var/log/trdp-simulator/trdp.pcap --pcap-max-size 8388608"
  ```
- Restart the service: `sudo systemctl restart trdp-simulator`.

### Raspberry Pi profile
- Configure with ARM-specific tuning flags during CMake configure:
  ```bash
  cmake -S . -B build -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=OFF -DTRDP_PI_OPTIMIZED=ON
  cmake --build build --target package
  ```
- Cross-compile from x86 using the provided toolchain file:
  ```bash
  cmake -S . -B build-arm64 -GNinja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm64-gnu.cmake \
    -DTRDP_USE_STUBS=ON -DTRDP_ENABLE_TESTS=OFF -DTRDP_PI_OPTIMIZED=ON
  cmake --build build-arm64 --target trdp-simulator
  ```
- Install the generated `.deb` on the Pi (`sudo apt install ./build/...deb`).
- Use the same `/etc/trdp-simulator` XML and `/etc/default/trdp-simulator` overrides as on VMs.
- For Docker on Pi: `docker run --platform linux/arm64/v8 ... trdp-simulator:pi`.

### Common runtime flags
- `--config /path/to/trdp.xml` – XML configuration file (installed to `/etc/trdp-simulator/trdp.xml`).
- `--use-trdp --trdp-lib <path> --trdp-include <dir>` – run against a proprietary TRDP SDK instead of stubs.
- PCAP controls: `--pcap-enable`, `--pcap-file <path>`, `--pcap-max-size <bytes>`, `--pcap-max-files <n>`, `--pcap-rx-only`, `--pcap-tx-only`.
- Logging: use `<Debug>` in XML or pass Drogon logging flags (e.g., `--logtostderr`).

## Verification checks
- `systemctl status trdp-simulator` shows the service as active and listening on `:8000`.
- `curl http://localhost:8000/api/diag/metrics` returns runtime metrics.
- `docker logs <container>` shows PD/MD engine start-up logs when running in a container.
