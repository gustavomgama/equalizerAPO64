#!/usr/bin/env bash
# Regression test for wide-string logging on Linux: glibc's fwprintf treats
# %s as narrow, so wide %s/%S specifiers must be swapped or paths are
# truncated to their first character. Asserts a full path appears in the log.
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK/in.wav" <<'PY'
import sys, wave, struct, math
with wave.open(sys.argv[1], "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
    for i in range(480):
        v = int(0.5 * 32767 * math.sin(2 * math.pi * 1000 * i / 48000))
        f.writeframes(struct.pack("<hh", v, v))
PY

printf 'Preamp: -6 dB\n' > "$WORK/config.txt"

XDG_STATE_HOME="$WORK" "$BIN" --in "$WORK/in.wav" --out "$WORK/out.wav" --config "$WORK/config.txt"

LOG="$WORK/equalizerapo/EqualizerAPO.log"
[ -f "$LOG" ] || { echo "FAIL: no log at $LOG"; exit 1; }

if grep -q "Loading configuration from $WORK/config.txt" "$LOG"; then
  echo "OK logging: full path present"
else
  echo "FAIL: full path not logged (wide %s truncation?)"
  grep "Loading configuration from" "$LOG" | tail -3
  exit 1
fi

# The level label must be a whole word, not a truncated first character.
if grep -qE '(TRACE|LOG) \(' "$LOG"; then
  echo "OK logging: level label intact"
else
  echo "FAIL: level label truncated (expected TRACE/LOG, got T/L)"
  head -2 "$LOG"
  exit 1
fi
