#!/usr/bin/env bash
# Verifies Linux `Device:` semantics: `Device: all` (and the sink name) apply
# the following block; a non-matching (e.g. Windows) device name skips it.
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK/in.wav" <<'PY'
import sys, wave, struct, math
with wave.open(sys.argv[1], "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
    for i in range(4800):
        v = int(0.5 * 32767 * math.sin(2 * math.pi * 1000 * i / 48000))
        f.writeframes(struct.pack("<hh", v, v))
PY

ratio() { # config-text -> prints output/input rms ratio
  printf '%s\n' "$1" > "$WORK/c.txt"
  "$BIN" --in "$WORK/in.wav" --out "$WORK/o.wav" --config "$WORK/c.txt"
  python3 - "$WORK/in.wav" "$WORK/o.wav" <<'PY'
import sys, wave, struct, math
def rms(p):
    with wave.open(p, "rb") as w:
        n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
    v = struct.unpack("<%dh" % (n * ch), raw)
    return math.sqrt(sum(x * x for x in v) / len(v))
print(rms(sys.argv[2]) / rms(sys.argv[1]))
PY
}

r_all=$(ratio "Device: all
Preamp: -6 dB")
r_named=$(ratio "Device: EqualizerAPO
Preamp: -6 dB")
r_windows=$(ratio "Device: Speakers
Preamp: -6 dB")

python3 - "$r_all" "$r_named" "$r_windows" <<'PY'
import sys
r_all, r_named, r_windows = map(float, sys.argv[1:4])
assert abs(r_all - 0.501) < 0.02, r_all
assert abs(r_named - 0.501) < 0.02, r_named
assert abs(r_windows - 1.0) < 0.02, r_windows
print("OK device routing")
PY
