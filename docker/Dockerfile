FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libspdlog-dev libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DDSE_BUILD_TESTS=ON \
    && cmake --build build -j$(nproc)

FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y \
    libgrpc++1 libprotobuf23 libspdlog1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/build/dse-metadata-service /usr/local/bin/
COPY --from=builder /build/build/dse-storage-node /usr/local/bin/
COPY --from=builder /build/build/storage /usr/local/bin/
COPY docker/configs/ /etc/dse/

RUN mkdir -p /storage

EXPOSE 50051 50053

CMD ["dse-metadata-service", "--address", "0.0.0.0:50051"]
