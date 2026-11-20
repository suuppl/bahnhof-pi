# Running in Docker

Instead of `make deploy` + systemd, the board can run as one container. The
image is built in CI for arm64 and published to
`ghcr.io/suuppl/bahnhof-pi` (`.github/workflows/image.yml`):

| Tag | Content |
|-----|---------|
| `sha-<commit>`, `main`, `latest`, `<version>` | runtime image: all programs, fonts, entrypoint |
| `…-sdk` (e.g. `sha-<commit>-sdk`) | build stage: toolchain, sources, build tree; base for your own plugins |

Pin `sha-<commit>` in production so a deploy never changes the version silently.

## Running

The container needs the framebuffer and the console TTY, plus the I²C bus and
the UART if you use the brightness / presence plugins. No `--privileged`.
See [`docker/docker-compose.example.yml`](../docker/docker-compose.example.yml):

```yaml
services:
  bahnhofstafel:
    image: ghcr.io/suuppl/bahnhof-pi:sha-<commit>
    restart: unless-stopped
    init: true
    devices:
      - /dev/fb0           # display
      - /dev/tty1          # console -> graphics mode (no text cursor over the board)
      - /dev/i2c-2         # brightness_ctl (DDC/CI), optional
      - /dev/ttyAMA0       # presence_ctl (radar UART), optional
    environment:
      TZ: Europe/Berlin    # IRIS times are German local time
    volumes:
      - ./bahnhofstafel.conf:/app/bahnhofstafel.conf:ro
      - ./line_colors.json:/app/line_colors.json:ro
      - cache:/app/cache
volumes:
  cache:
```

- **Settings:** the same `bahnhofstafel.conf` / `line_colors.json` as on bare metal, mounted into `/app`.
  `APP_DIR` is `/app`, so paths in the conf refer to the container.
- **Plugins:** by default every plugin in the image starts; `PLUGINS="…"` in the conf picks them.
  A plugin that exits (e.g. `presence_ctl` without a sensor) doesn't stop the container; if
  `iris_fetch` or the display exits, the container exits and `restart:` brings it back.
- **Logs:** all programs log to stdout (`docker logs bahnhofstafel`).
- **Remove a device line** if the hardware isn't there; Docker refuses to start with a missing device.

### Host preparation (once)

- Install Docker; stop and disable the bare-metal setup: `sudo systemctl disable --now bahnhofstafel.service`.
- Fbcon cursor blink lives in sysfs, which is read-only in containers. If the cursor still
  shows, keep a host boot step: `echo 0 > /sys/class/graphics/fbcon/cursor_blink`.
- Hotkeys (triggerhappy): point them at `docker restart bahnhofstafel` / `docker stop bahnhofstafel`.

## Your own plugins

Single-file C plugins (one `*.c` = one daemon, linked against `bahnhof_common`: `http.c`,
`fileio.c`, `settings.c`, `screen.c`, curl, cJSON) can be layered onto a published image:

```dockerfile
ARG BAHNHOF_IMAGE=ghcr.io/suuppl/bahnhof-pi:sha-<commit>
FROM ${BAHNHOF_IMAGE}-sdk AS build
COPY plugins/ /src/plugins/
RUN cmake -S /src -B /build -DBAHNHOF_PLUGIN_DIR=/src/plugins \
 && mkdir -p /plugins-out \
 && for f in /src/plugins/*.c; do n=$(basename "$f" .c); cmake --build /build --target "$n" && cp "/build/$n" /plugins-out/; done

FROM ${BAHNHOF_IMAGE}
COPY --from=build /plugins-out/ /app/bin/
```

This only compiles your files (seconds, also on the Pi itself). They start automatically
with the bundled plugins and read their own settings from `bahnhofstafel.conf` via
`cfg_str()` / `cfg_long()`.

Building the whole image from source with plugins works too:
`docker buildx build --platform linux/arm64 --build-context plugins=./plugins -t bahnhofstafel .`

## Building locally

```sh
docker buildx build --platform linux/arm64 -t bahnhofstafel .        # on an arm64 host, or with QEMU
```

The build runs inside Debian (`DEBIAN_RELEASE`, default `bookworm`), so it needs neither
the cross compiler nor `make sysroot`.
