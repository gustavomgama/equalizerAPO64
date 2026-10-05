#!/usr/bin/env bash
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK/in.wav" <<'PY'
import sys, wave, struct, math
path = sys.argv[1]
rate, dur, amp = 48000, 1.0, 0.5
with wave.open(path, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    for i in range(int(rate * dur)):
        v = int(amp * 32767 * math.sin(2 * math.pi * 1000 * i / rate))
        w.writeframes(struct.pack("<hh", v, v))
PY

cat > "$WORK/config.txt" <<'EOF'
Preamp: -6 dB
EOF

"$BIN" --in "$WORK/in.wav" --out "$WORK/out.wav" --config "$WORK/config.txt"

python3 - "$WORK/in.wav" "$WORK/out.wav" <<'PY'
import sys, wave, struct, math
def rms(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
    vals = struct.unpack("<%dh" % (n * ch), raw)
    return math.sqrt(sum(v * v for v in vals) / len(vals))
ratio = rms(sys.argv[2]) / rms(sys.argv[1])
expected = 10 ** (-6 / 20)
assert abs(ratio - expected) < 0.01, (ratio, expected)
print("OK", ratio)
PY
