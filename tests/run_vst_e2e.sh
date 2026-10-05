#!/usr/bin/env bash
# Runs eqapo-null with a config that loads the stub VST2 plugin and asserts the
# plugin's 0.5 gain was applied.
set -euo pipefail
BIN="$1"
STUB="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK/in.wav" <<'PY'
import sys, wave, struct, math
rate, dur, amp = 48000, 0.5, 0.5
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    for i in range(int(rate * dur)):
        v = int(amp * 32767 * math.sin(2 * math.pi * 1000 * i / rate))
        w.writeframes(struct.pack("<hh", v, v))
PY

printf 'VSTPlugin: Library %s\n' "$STUB" > "$WORK/config.txt"

"$BIN" --in "$WORK/in.wav" --out "$WORK/out.wav" --config "$WORK/config.txt"

python3 - "$WORK/in.wav" "$WORK/out.wav" <<'PY'
import sys, wave, struct, math
def rms(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
    vals = struct.unpack("<%dh" % (n * ch), raw)
    return math.sqrt(sum(v * v for v in vals) / len(vals))
ratio = rms(sys.argv[2]) / rms(sys.argv[1])
assert abs(ratio - 0.5) < 0.02, ratio
print("OK VST ratio", ratio)
PY
