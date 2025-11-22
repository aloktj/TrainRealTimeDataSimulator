# Multi-stage build for the TRDP simulator
FROM ubuntu:22.04 AS build
ARG DEBIAN_FRONTEND=noninteractive

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

RUN cmake -S . -B build -GNinja \
        -DTRDP_USE_STUBS=ON \
        -DTRDP_ENABLE_TESTS=OFF \
    && cmake --build build --target trdp-simulator \
    && cmake --install build --prefix /install

FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libssl3 \
        libdrogon-dev \
        libtinyxml2-9 \
        nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /install/ /usr/

EXPOSE 8848
ENTRYPOINT ["/usr/bin/trdp-simulator"]
CMD ["--config", "/etc/trdp-simulator/trdp.xml"]
