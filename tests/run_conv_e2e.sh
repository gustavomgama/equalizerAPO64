#!/usr/bin/env bash
# Verifies Convolution performs a real time-domain convolution: a dirac IR
# keeps the impulse at sample 0, a 10-sample-delayed IR shifts it to sample 10.
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK" <<'PY'
import sys, wave, struct
w = sys.argv[1]
def write(path, vals, n=512):
    with wave.open(path, "wb") as f:
        f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
        for i in range(n):
            v = vals[i] if i < len(vals) else 0
            f.writeframes(struct.pack("<hh", v, v))
write(w + "/in.wav", [30000])
write(w + "/dirac.wav", [30000])
write(w + "/delayed.wav", [0] * 10 + [30000])
PY

for ir in dirac delayed; do
  printf 'Convolution: %s/%s.wav\n' "$WORK" "$ir" > "$WORK/c_$ir.txt"
  "$BIN" --in "$WORK/in.wav" --out "$WORK/out_$ir.wav" --config "$WORK/c_$ir.txt"
done

python3 - "$WORK" <<'PY'
import sys, wave, struct
w = sys.argv[1]
def first_peak(path):
    with wave.open(path, "rb") as f:
        n = f.getnframes(); raw = f.readframes(n); ch = f.getnchannels()
    v = struct.unpack("<%dh" % (n * ch), raw)
    mono = [v[i] for i in range(0, len(v), ch)]
    m = max(abs(x) for x in mono)
    if m == 0:
        return -1
    for i, x in enumerate(mono):
        if abs(x) > 0.1 * m:
            return i
    return -1
d = first_peak(w + "/out_dirac.wav")
dl = first_peak(w + "/out_delayed.wav")
assert d == 0, ("dirac not at 0", d)
assert dl == 10, ("delayed not at 10", dl)
print("OK convolution dirac@%d delayed@%d" % (d, dl))
PY
