#!/usr/bin/env bash
# Hot-reload robustness: the host watches config.txt and must survive rapid
# rewrites. Uses an isolated XDG_CONFIG_HOME/XDG_STATE_HOME so it never touches
# the user's real config. Skips if PipeWire is not running.
set -euo pipefail
BIN="$1"

if ! pw-cli info 0 >/dev/null 2>&1; then
  echo "SKIP: PipeWire not running"
  exit 0
fi

WORK="$(mktemp -d)"
mkdir -p "$WORK/equalizerapo"
printf 'Preamp: -3 dB\n' > "$WORK/equalizerapo/config.txt"

export XDG_CONFIG_HOME="$WORK"
export XDG_STATE_HOME="$WORK"

"$BIN" --name EQPRELOAD >"$WORK/host.stderr" 2>&1 &
HPID=$!
trap 'kill "$HPID" 2>/dev/null || true; wait "$HPID" 2>/dev/null || true; rm -rf "$WORK"' EXIT

sleep 2

for i in 1 2 3 4 5 6 7 8; do
  printf 'Preamp: -%d dB\nFilter 1: ON PK Fc %d Hz Gain -3 dB Q 1\n' "$i" "$((100 * i))" \
    > "$WORK/equalizerapo/config.txt"
  sleep 0.25
done

sleep 1

if ! kill -0 "$HPID" 2>/dev/null; then
  echo "FAIL: host died during rapid config reload"
  cat "$WORK/host.stderr" || true
  exit 1
fi

LOG="$WORK/equalizerapo/EqualizerAPO.log"
reloads=0
[ -f "$LOG" ] && reloads=$(grep -c 'Finished loading configuration' "$LOG" || true)
echo "OK hot-reload survived, reloads=$reloads"
