# Builds the solver server and the web demo into a small image.
#
#   docker build -t cubesolver .
#   docker run -p 8080:8080 -v cubesolver-data:/data cubesolver
#
# Hosting services set PORT themselves. The visitor and solve counts are saved
# in /data; mount a volume there to keep them across restarts and deploys.

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY third_party third_party
COPY web web
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUBESOLVER_BUILD_TESTS=OFF \
 && cmake --build build -j --target cubesolver_server

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/build/cubesolver_server /app/cubesolver_server
COPY web /app/web
RUN mkdir -p /data
ENV PORT=8080 \
    CUBESOLVER_DATA_FILE=/data/counters.txt
EXPOSE 8080
CMD ["/app/cubesolver_server", "--host", "0.0.0.0", "--web-dir", "/app/web"]
