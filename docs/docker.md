# Running in Docker

Instead of `make deploy` + systemd, the board can run as one container that
Docker builds **on the Pi itself**, straight from this git repository: the
`Dockerfile` compiles everything inside Debian (`DEBIAN_RELEASE`, default
`bookworm`), so there's no cross compiler, no `make sysroot` and no registry.

## Compose

See [`docker/docker-compose.example.yml`](../docker/docker-compose.example.yml):

```yaml
services:
  bahnhofstafel:
    build:
      context: https://github.com/suuppl/bahnhof-pi.git#<full-commit-sha>
      additional_contexts:          # optional, your own plugins
        plugins: ./plugins
    image: bahnhofstafel:local
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

`docker compose up -d --build` builds and starts it.

- **Version pin:** the build context is the repo at a **full** commit hash (BuildKit can't
  resolve abbreviated ones); bump it to update. Branch names (`#main`) work too, but then
  each build may pick up new commits.
- **Build time:** the first build compiles LVGL and takes a few minutes on a Pi 4 (longer
  on older models). Later `--build` runs with the same pin and plugins hit the build cache.
- **Settings:** the same `bahnhofstafel.conf` / `line_colors.json` as on bare metal, mounted
  into `/app` (`APP_DIR`), so paths in the conf refer to the container.
- **Plugins:** by default every plugin in the image starts; `PLUGINS="…"` in the conf picks
  them. A plugin that exits (e.g. `presence_ctl` / `brightness_ctl` without their hardware)
  doesn't stop the container; if `iris_fetch` or the display exits, the container exits and
  `restart:` brings it back.
- **Logs:** all programs log to stdout (`docker logs bahnhofstafel`).
- **Devices:** remove a line if the hardware isn't there; Docker refuses to start with a
  missing device. No `--privileged` needed.

## Your own plugins

Single-file C plugins (one `plugins/<name>.c` = one daemon `<name>`, linked against
`bahnhof_common`: `http.c`, `fileio.c`, `settings.c`, `screen.c`, curl, cJSON) are compiled
into the image when you pass the directory as the `plugins` build context (compose
`additional_contexts` as above, or `docker build --build-context plugins=./plugins`). They
start with the bundled plugins and read their own settings from `bahnhofstafel.conf` via
`cfg_str()` / `cfg_long()`. See [`plugins.md`](plugins.md) for the bar/screen interfaces.

## Host preparation (once)

- Docker with Compose v2 (`additional_contexts` needs Compose ≥ 2.17).
- Stop and disable the bare-metal setup: `sudo systemctl disable --now bahnhofstafel.service`.
- Fbcon cursor blink lives in sysfs, which is read-only in containers. If the cursor still
  shows, keep a host boot step: `echo 0 > /sys/class/graphics/fbcon/cursor_blink`.
- Hotkeys (triggerhappy): point them at `docker stop bahnhofstafel` / `docker restart bahnhofstafel`.

## Stages

`plugins` (empty unless overridden by the build context) → `sdk` (Debian + build deps +
sources + build tree) → `runtime` (default target, ~200 MB).
