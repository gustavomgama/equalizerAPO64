#!/usr/bin/env bash
# End-to-end test of the full filter chain on Linux: Preamp, GraphicEQ, BiQuad,
# Delay, Copy, LoudnessCorrection, Convolution, and a VST2 plugin, all in one
# config. Asserts eqapo-null completes and produces finite output.
set -euo pipefail
BIN="$1"
STUB="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK" <<'PY'
import sys, wave, struct, math
w = sys.argv[1]
with wave.open(w + "/in.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
    for i in range(24000):
        v = int(0.5 * 32767 * math.sin(2 * math.pi * 1000 * i / 48000))
        f.writeframes(struct.pack("<hh", v, v))
with wave.open(w + "/ir.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
    f.writeframes(struct.pack("<hh", 32767, 32767) + b"\x00\x00\x00\x00" * 99)
PY

cp "$STUB" "$WORK/stub.so"

cat > "$WORK/config.txt" <<EOF
Preamp: -3 dB
GraphicEQ: 25 0; 1000 3; 16000 -3
Filter 1: ON PK Fc 2000 Hz Gain -6 dB Q 1.5
Delay: 5 ms
Copy: L=R
LoudnessCorrection: State 1 ReferenceLevel -10 ReferenceOffset 0
Convolution: $WORK/ir.wav
VSTPlugin: Library $WORK/stub.so
EOF

"$BIN" --in "$WORK/in.wav" --out "$WORK/out.wav" --config "$WORK/config.txt"

python3 - "$WORK/out.wav" <<'PY'
import sys, wave, struct
with wave.open(sys.argv[1], "rb") as w:
    n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
vals = struct.unpack("<%dh" % (n * ch), raw)
assert n == 24000, n
assert all(abs(v) <= 32767 for v in vals)
assert any(v != 0 for v in vals)
print("OK full chain, frames=%d" % n)
PY
