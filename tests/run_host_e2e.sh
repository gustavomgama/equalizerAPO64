#!/usr/bin/env bash
# Verifies eqapo-host creates its virtual sink and links its output to the
# default sink. Skips cleanly if PipeWire is not running.
set -euo pipefail
BIN="$1"

if ! pw-cli info 0 >/dev/null 2>&1; then
  echo "SKIP: PipeWire not running"
  exit 0
fi

LOG="$(mktemp)"
"$BIN" --name EqualizerAPO >"$LOG" 2>&1 &
HPID=$!
trap 'kill "$HPID" 2>/dev/null || true; rm -f "$LOG"' EXIT

# Wait for the virtual sink to appear.
found=0
for _ in $(seq 1 25); do
  if pw-cli ls Node 2>/dev/null | grep -q 'media.class = "Audio/Sink"' \
     && pw-cli ls Node 2>/dev/null | grep -q 'node.name = "EqualizerAPO"'; then
    found=1
    break
  fi
  sleep 0.2
done
if [ "$found" -ne 1 ]; then
  echo "FAIL: EqualizerAPO virtual sink did not appear"
  cat "$LOG"
  exit 1
fi

# Wait for the output stream to link to the default sink.
linked=0
for _ in $(seq 1 25); do
  if pw-link -l 2>/dev/null | grep -q 'eqapo_output:output_FL'; then
    linked=1
    break
  fi
  sleep 0.2
done
if [ "$linked" -ne 1 ]; then
  echo "FAIL: eqapo_output did not link to the default sink"
  pw-link -l 2>/dev/null || true
  exit 1
fi

echo "OK: virtual sink present and linked to default sink"
