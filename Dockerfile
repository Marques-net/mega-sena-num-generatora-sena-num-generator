FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel

FROM registry-1.docker.io/bitnami/mongodb:latest

COPY --from=build /src/build/mega_sena_num_generator /usr/local/bin/mega_sena_num_generator
COPY --from=build /src/build/mega_sena_calibration_agent /usr/local/bin/mega_sena_calibration_agent

ENV PATH="/opt/bitnami/mongodb/bin:${PATH}"

USER 10001:65534
EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/mega_sena_num_generator"]
CMD ["--serve", "--port", "8080"]
