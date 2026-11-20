#!/bin/bash
# Container entrypoint: starts the core (iris_fetch + display) and the
# plugins, logs to stdout, and exits (-> container restart) if a core
# process dies. Plugins may exit on their own (e.g. presence_ctl without a
# sensor) without taking the board down.
#
# PLUGINS (from bahnhofstafel.conf or the environment) picks the plugins;
# default: every binary in /app/bin except the core ones.
set -u

APP=${APP_DIR:-/app}
BIN=$APP/bin
export APP_DIR=$APP

# settings (shell-compatible KEY=value), also read by the programs themselves
if [ -f "$APP/bahnhofstafel.conf" ]; then
    set -a
    # shellcheck source=/dev/null
    source "$APP/bahnhofstafel.conf"
    set +a
fi

if [ -z "${PLUGINS:-}" ]; then
    PLUGINS=$(cd "$BIN" && ls | grep -vxE 'iris_fetch|bahnhofstafel' | tr '\n' ' ')
fi

mkdir -p /run/bahnhofstafel "$APP/cache"

# line-buffered output so every process' log lines reach `docker logs` promptly
RUN=(stdbuf -oL -eL)   # execs the program, so $! is its PID

pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null; wait; exit 0; }
trap cleanup TERM INT

"${RUN[@]}" "$BIN/iris_fetch" &
core=($!)
pids+=($!)
for p in $PLUGINS; do
    if [ -x "$BIN/$p" ]; then
        "${RUN[@]}" "$BIN/$p" &
        pids+=($!)
    else
        echo "entrypoint: plugin $p not found in $BIN" >&2
    fi
done
sleep 3   # let fetchers populate data before the display starts
"${RUN[@]}" "$BIN/bahnhofstafel" &
core+=($!)
pids+=($!)
echo "entrypoint: started iris_fetch bahnhofstafel $PLUGINS"

# wait until a core process exits, then stop everything
wait -n "${core[@]}"
rc=$?
echo "entrypoint: core process exited ($rc), stopping" >&2
kill "${pids[@]}" 2>/dev/null
wait
exit "$rc"
