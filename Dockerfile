# Multi-stage build for the TRDP simulator
FROM ubuntu:22.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        libssl-dev \
        libdrogon-dev \
        libtinyxml2-dev \
        nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN if [ "$TARGETARCH" = "arm" ] || [ "$TARGETARCH" = "arm64" ]; then PI_FLAGS="-DTRDP_PI_OPTIMIZED=ON"; else PI_FLAGS=""; fi \
    && cmake -S . -B build -GNinja \
        -DTRDP_USE_STUBS=ON \
        -DTRDP_ENABLE_TESTS=OFF \
        ${PI_FLAGS} \
    && cmake --build build --target trdp-simulator \
    && cmake --install build --prefix /install

FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libssl3 \
        libdrogon-dev \
        libtinyxml2-9 \
        nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /install/ /usr/

RUN install -d /etc/trdp-simulator /etc/default \
    && if [ -f /usr/etc/trdp-simulator/trdp.xml ]; then cp /usr/etc/trdp-simulator/trdp.xml /etc/trdp-simulator/; fi \
    && if [ -f /usr/etc/default/trdp-simulator ]; then cp /usr/etc/default/trdp-simulator /etc/default/; fi

LABEL org.opencontainers.image.title="TRDP Simulator" \
      org.opencontainers.image.description="TRDP simulator with HTTP API, PD/MD controls, and diagnostics" \
      org.opencontainers.image.version="0.1.0" \
      org.opencontainers.image.source="https://example.com/trdp-simulator"

EXPOSE 8000
ENTRYPOINT ["/usr/bin/trdp-simulator"]
CMD ["--config", "/etc/trdp-simulator/trdp.xml"]
