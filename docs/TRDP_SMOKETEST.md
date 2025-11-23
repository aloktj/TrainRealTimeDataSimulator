# TRDP publish smoketest

The repository now includes a minimal TRDP publisher that can be used to
validate socket-level and TRDP stack configuration outside of the full
simulator. The tool is only built when TRDP stubs are disabled and a real
TRDP library is available (for example with `-DTRDP_USE_TCNOPEN=ON`).

## Building

```
cmake -S . -B build \
    -DTRDP_USE_STUBS=OFF \
    -DTRDP_USE_TCNOPEN=ON \
    -DTRDP_ENABLE_APP=OFF \
    -DTRDP_ENABLE_TESTS=OFF
cmake --build build --target trdp-publish-smoketest
```

Adjust the TRDP options if you are linking against a system-installed TRDP
library instead of the TCNopen submodule.

## Running

```
./build/trdp-publish-smoketest <src_ip> <dst_ip> <dst_port> [com_id]
```

Example (matching the environment check in the issue):

```
./build/trdp-publish-smoketest 192.168.56.100 239.1.1.1 17224 1000
```

The program prints the return value from `tlp_publish()`. A zero exit code
indicates success; a non-zero exit code prints the TRDP error code to help
pinpoint configuration issues (e.g., `-6` for `TRDP_SOCK_ERR`).
