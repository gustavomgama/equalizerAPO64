#!/usr/bin/env bash
# Verifies relative Include and Convolution paths resolve against the config
# file's directory (std::filesystem path handling on Linux).
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/sub"
python3 - "$WORK" <<'PY'
import sys, wave, struct, math
w = sys.argv[1]
with wave.open(w + "/in.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
    for i in range(4800):
        v = int(0.5 * 32767 * math.sin(2 * math.pi * 1000 * i / 48000))
        f.writeframes(struct.pack("<hh", v, v))
with wave.open(w + "/sub/ir.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
    f.writeframes(struct.pack("<hh", 32767, 32767) + b"\x00\x00\x00\x00" * 9)
PY

printf 'Preamp: -6 dB\n' > "$WORK/sub/child.txt"
printf 'Include: sub/child.txt\nConvolution: sub/ir.wav\n' > "$WORK/config.txt"

"$BIN" --in "$WORK/in.wav" --out "$WORK/out.wav" --config "$WORK/config.txt"

python3 - "$WORK/in.wav" "$WORK/out.wav" <<'PY'
import sys, wave, struct, math
def rms(p):
    with wave.open(p, "rb") as w:
        n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
    v = struct.unpack("<%dh" % (n * ch), raw)
    return math.sqrt(sum(x * x for x in v) / len(v))
ratio = rms(sys.argv[2]) / rms(sys.argv[1])
assert abs(ratio - 0.501) < 0.02, ratio
print("OK relative paths, ratio=%.3f" % ratio)
PY
