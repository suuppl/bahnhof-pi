# syntax=docker/dockerfile:1
#
# Bahnhofstafel container image (see docs/docker.md).
#
#   docker buildx build --platform linux/arm64 -t bahnhofstafel .
#
# Stages:
#   plugins  empty by default; override with a named build context to compile
#            your own single-file plugins in (`--build-context plugins=./dir`)
#   sdk      Debian + build deps + sources + configured build tree; also used
#            as a base to compile plugins against an already published image
#   runtime  the image that runs on the Pi (default target)

ARG DEBIAN_RELEASE=bookworm

FROM scratch AS plugins

FROM debian:${DEBIAN_RELEASE}-slim AS sdk
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake \
      libcurl4-openssl-dev libxml2-dev libcjson-dev libfreetype-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . /src
COPY --from=plugins / /src/plugins/
RUN cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release -DBAHNHOF_PLUGIN_DIR=/src/plugins \
 && cmake --build /build -j"$(nproc)" \
 && mkdir -p /out/bin \
 && find /build -maxdepth 1 -type f -executable -exec cp {} /out/bin/ \; \
 && ls /out/bin

FROM debian:${DEBIAN_RELEASE}-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libcurl4 libxml2 libcjson1 libfreetype6 \
      fonts-dejavu-core ca-certificates tzdata \
 && rm -rf /var/lib/apt/lists/*
COPY --from=sdk /out/bin/ /app/bin/
COPY fonts/ /app/fonts/
COPY THIRD_PARTY.md LICENSE /app/
COPY docker/entrypoint.sh /app/entrypoint.sh
# Settings: mount bahnhofstafel.conf and line_colors.json into /app,
# persist /app/cache (EVA + weather cache). IRIS times are German local time.
ENV APP_DIR=/app TZ=Europe/Berlin
VOLUME /app/cache
ENTRYPOINT ["/app/entrypoint.sh"]
