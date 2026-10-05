#!/usr/bin/env bash
# DSP correctness (not just "finite output"): a 1 kHz low-pass must pass a
# 100 Hz tone nearly unchanged and strongly attenuate a 10 kHz tone. This
# exercises the actual filter math (BiQuad + SIMD/FMA path) on Linux.
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK" <<'PY'
import sys, wave, struct, math
w = sys.argv[1]
for name, freq in (("low", 100), ("mid", 1000), ("high", 10000)):
    with wave.open("%s/%s.wav" % (w, name), "wb") as f:
        f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
        for i in range(48000):
            v = int(0.5 * 32767 * math.sin(2 * math.pi * freq * i / 48000))
            f.writeframes(struct.pack("<hh", v, v))
PY

rms() { python3 -c "
import wave, struct, math
with wave.open('$1','rb') as w:
    n=w.getnframes(); raw=w.readframes(n); ch=w.getnchannels()
v=struct.unpack('<%dh'%(n*ch), raw)
print(math.sqrt(sum(x*x for x in v)/len(v)))
"; }

# 1) Low-pass: 100 Hz passes, 10 kHz is cut.
printf 'Filter 1: ON LP Fc 1000 Hz Q 0.707\n' > "$WORK/config.txt"
for name in low high; do
  "$BIN" --in "$WORK/$name.wav" --out "$WORK/${name}_out.wav" --config "$WORK/config.txt"
done

in_low=$(rms "$WORK/low.wav");  out_low=$(rms "$WORK/low_out.wav")
in_high=$(rms "$WORK/high.wav"); out_high=$(rms "$WORK/high_out.wav")

python3 - "$in_low" "$out_low" "$in_high" "$out_high" <<'PY'
import sys
in_low, out_low, in_high, out_high = map(float, sys.argv[1:5])
g_low = out_low / in_low
g_high = out_high / in_high
# 100 Hz should pass (>0.8); 10 kHz should be cut hard (<0.15).
assert g_low > 0.8, ("low not passed", g_low)
assert g_high < 0.15, ("high not attenuated", g_high)
assert g_low > 5 * g_high, ("low/high separation too small", g_low, g_high)
print("OK dsp lowpass gain100=%.3f gain10k=%.3f" % (g_low, g_high))
PY

# 2) GraphicEQ: +6 dB at 1 kHz, -6 dB at 10 kHz.
printf 'GraphicEQ: 100 0; 1000 6; 10000 -6\n' > "$WORK/geq.txt"
for name in mid high; do
  "$BIN" --in "$WORK/$name.wav" --out "$WORK/${name}_geq.wav" --config "$WORK/geq.txt"
done

in_mid=$(rms "$WORK/mid.wav");   out_mid=$(rms "$WORK/mid_geq.wav")
in_high2=$(rms "$WORK/high.wav"); out_high2=$(rms "$WORK/high_geq.wav")

python3 - "$in_mid" "$out_mid" "$in_high2" "$out_high2" <<'PY'
import sys
in_mid, out_mid, in_high, out_high = map(float, sys.argv[1:5])
g_mid = out_mid / in_mid
g_high = out_high / in_high
assert abs(g_mid - 2.0) < 0.3, ("1k boost wrong", g_mid)
assert abs(g_high - 0.5) < 0.1, ("10k cut wrong", g_high)
print("OK dsp graphiceq gain1k=%.3f gain10k=%.3f" % (g_mid, g_high))
PY
