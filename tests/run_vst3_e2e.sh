#!/usr/bin/env bash
# Runs eqapo-null with configs that load the stub VST3 plugin (direct .so and
# .vst3 bundle layout) and asserts the plugin's 0.5 gain was applied.
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

check_ratio() {
python3 - "$WORK/in.wav" "$1" "$2" <<'PY'
import sys, wave, struct, math
def rms(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
    vals = struct.unpack("<%dh" % (n * ch), raw)
    return math.sqrt(sum(v * v for v in vals) / len(vals))
ratio = rms(sys.argv[2]) / rms(sys.argv[1])
assert abs(ratio - 0.5) < 0.03, (sys.argv[3], ratio)
print("OK VST3 ratio", sys.argv[3], ratio)
PY
}

# Direct .so reference with an explicit Gain param.
printf 'VSTPlugin: Library "%s" Gain 0.5\n' "$STUB" > "$WORK/config-so.txt"
"$BIN" --in "$WORK/in.wav" --out "$WORK/out-so.wav" --config "$WORK/config-so.txt"
check_ratio "$WORK/out-so.wav" direct-so

# .vst3 bundle layout (what installers and yabridge produce).
mkdir -p "$WORK/Gain.vst3/Contents/x86_64-linux"
cp "$STUB" "$WORK/Gain.vst3/Contents/x86_64-linux/Gain.so"
printf 'VSTPlugin: Library "%s"\n' "$WORK/Gain.vst3" > "$WORK/config-bundle.txt"
"$BIN" --in "$WORK/in.wav" --out "$WORK/out-bundle.wav" --config "$WORK/config-bundle.txt"
check_ratio "$WORK/out-bundle.wav" bundle
