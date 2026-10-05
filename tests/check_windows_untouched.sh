#!/usr/bin/env bash
set -euo pipefail

# Use the source dir passed by CMake when available, so out-of-tree builds
# (e.g. cmake -B /tmp/build) still work. Fall back to the git root.
if [ "${1:-}" != "" ]; then
  cd "$1"
else
  cd "$(git rev-parse --show-toplevel)"
fi

# 1. Windows-only build files must never be modified by this work.
bad=$(git status --porcelain | awk '{print $2}' | grep -E '\.(vcxproj|sln|bat|pro|rc)$' || true)
if [ -n "$bad" ]; then
  echo "FAIL: Windows build files modified:"
  echo "$bad"
  exit 1
fi

# 2. Any modified shared source that originally contained Windows-specific
#    code must still contain a #ifdef _WIN32 guard. Compare against the
#    pre-port base (origin/main) so guards added by the port itself do not
#    cause false positives; fall back to HEAD if there is no origin.
BASE=""
if git rev-parse --verify -q origin/main >/dev/null 2>&1; then
  BASE="origin/main"
elif git rev-parse --verify -q HEAD >/dev/null 2>&1; then
  BASE="HEAD"
fi

fail=0
for f in $(git status --porcelain | awk '{print $2}' | grep -E '\.(cpp|h)$' || true); do
  case "$f" in
    helpers/ConfigPathHelper.*|linux/*|tests/*) continue ;;
  esac
  if [ -n "$BASE" ] && git show "$BASE:$f" 2>/dev/null | grep -qE 'windows\.h|_WIN32|CreateFile|LoadLibrary|CRITICAL_SECTION|CreateSemaphore|QueryPerformanceCounter|IMMDevice'; then
    if ! grep -q '_WIN32' "$f"; then
      echo "FAIL: Windows guard missing in modified file $f"
      fail=1
    fi
  fi
done
[ "$fail" -eq 0 ] || exit 1
echo "OK: Windows port untouched"
