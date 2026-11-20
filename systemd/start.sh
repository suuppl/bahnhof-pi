#!/bin/bash
# Start the bahnhofstafel core (iris_fetch + display) and its plugins.
#
# Plugins to start come from PLUGINS in bahnhofstafel.conf (space separated).
# Default: the bundled ones plus any extra daemons an embedding project
# deployed (EXTRA_BINS → extra_daemons).

APP=$(dirname "$(readlink -f "$0")")
BIN=$APP/bin
LOG=$APP/log
# programs find bahnhofstafel.conf (runtime settings) via APP_DIR
export APP_DIR="$APP"
EXTRA=$(cat "$APP/extra_daemons" 2>/dev/null)

# Settings (shell-compatible KEY=value) and secrets
set -a
# shellcheck source=/dev/null
[ -f "$APP/bahnhofstafel.conf" ] && source "$APP/bahnhofstafel.conf"
# shellcheck source=/dev/null
[ -f "$APP/.env" ] && source "$APP/.env"
set +a
PLUGINS=${PLUGINS:-"weather_fetch brightness_ctl presence_ctl $EXTRA"}

mkdir -p /run/bahnhofstafel "$LOG"

# stop everything we might have started before (by exact process name)
for d in bahnhofstafel iris_fetch weather_fetch brightness_ctl presence_ctl $EXTRA $PLUGINS; do
    pkill -9 -x "$d"
done
sleep 1
# drop requests/groups of plugins that are no longer running
rm -rf /run/bahnhofstafel/bar.d /run/bahnhofstafel/screen.d

# Disable console cursor on framebuffer
printf '\033[?25l' > /dev/tty1 2>/dev/null || true

"$BIN/iris_fetch" >> "$LOG/iris.log" 2>&1 &
for d in $PLUGINS; do
    if [ -x "$BIN/$d" ]; then "$BIN/$d" >> "$LOG/$d.log" 2>&1 &
    else echo "start.sh: plugin $d not found in $BIN" >&2; fi
done
sleep 3   # let fetchers populate data before display starts
"$BIN/bahnhofstafel" >> "$LOG/display.log" 2>&1 &

echo "bahnhofstafel started: iris_fetch bahnhofstafel $PLUGINS"
