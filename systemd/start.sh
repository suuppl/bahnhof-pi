#!/bin/bash
# Start all bahnhofstafel processes

APP=$(dirname "$(readlink -f "$0")")
BIN=$APP/bin
LOG=$APP/log
# extra daemons from an embedding project (written by `make deploy`)
EXTRA=$(cat "$APP/extra_daemons" 2>/dev/null)

# Load secrets from .env, if present (none of the current daemons require one)
if [ -f "$APP/.env" ]; then
    set -a
    # shellcheck source=/dev/null
    source "$APP/.env"
    set +a
fi

mkdir -p /run/bahnhofstafel "$LOG"

# pkill takes a single pattern — kill each by exact process name separately
pkill -9 -x iris_fetch
pkill -9 -x weather_fetch
for d in $EXTRA; do pkill -9 -x "$d"; done
pkill -9 -x brightness_ctl
pkill -9 -x bahnhofstafel
sleep 1

# Disable console cursor on framebuffer
printf '\033[?25l' > /dev/tty1 2>/dev/null || true

$BIN/iris_fetch    >> "$LOG/iris.log"     2>&1 &
$BIN/weather_fetch >> "$LOG/weather.log"  2>&1 &
for d in $EXTRA; do "$BIN/$d" >> "$LOG/$d.log" 2>&1 & done
$BIN/brightness_ctl >> "$LOG/brightness.log" 2>&1 &
sleep 3   # let fetchers populate data before display starts
$BIN/bahnhofstafel >> "$LOG/display.log"  2>&1 &

echo "bahnhofstafel started (pids: $(pgrep -u "$(id -u)" -f "$BIN/" | tr '\n' ' '))"
