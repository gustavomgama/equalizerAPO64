#!/usr/bin/env bash
# Set up Wine + yabridge so Windows VST2 (.dll) and VST3 (.vst3) plugins work
# with eqapo-host. The host auto-maps a Windows plugin path to its yabridge
# wrapper, so you can keep the Windows path in config.txt.
#
# Usage:
#   bash packaging/arch/yabridge-setup.sh "<windows plugin dir>" [...]
# Example:
#   bash packaging/arch/yabridge-setup.sh "/run/media/helmet/DRIVE/Program Files/Steinberg/VSTPlugins"
set -euo pipefail

fail=0
command -v wine >/dev/null 2>&1 || { echo "missing wine: sudo pacman -S wine"; fail=1; }
command -v yabridgectl >/dev/null 2>&1 || { echo "missing yabridge: install yabridge or yabridge-bin from the AUR, then re-run"; fail=1; }
[ "$fail" -ne 0 ] && exit 1

if [ "$#" -eq 0 ]; then
	echo "usage: $0 \"<windows plugin dir>\" [...]"
	echo "current yabridge dirs:"; yabridgectl list || true
	exit 2
fi

for d in "$@"; do
	yabridgectl add "$d"
done
yabridgectl sync

echo
echo "Wrappers: ~/.vst/yabridge/<name>.so (VST2) and ~/.vst3/yabridge/<name>.vst3 (VST3)."
echo "Keep the Windows path in config, e.g.: VSTPlugin: Library \"/path/to/Foo.dll\""
