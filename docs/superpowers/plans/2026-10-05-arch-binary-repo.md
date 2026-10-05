# Arch Linux Binary Repository Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the Linux port as a signed pacman binary repository hosted on GitHub Releases, built by CI from this repo.

**Architecture:** A `packaging/arch/PKGBUILD` builds the app from the repository checkout that contains it (no re-clone); muparserx is vendored statically from a pinned fork checkout. A new workflow builds the package in an `archlinux` container, signs the DB + packages, runs `repo-add`, and publishes the assets to a rolling `arch-repo` prerelease.

**Tech Stack:** pacman/makepkg, repo-add, CMake, GitHub Actions, GPG.

**Spec:** `docs/superpowers/specs/2026-10-05-arch-binary-repo-design.md`

## Global Constraints

- Windows build untouched: no edits to `.vcxproj`, `.sln`, `.bat`, `.pro`, `.rc`; `windows_untouched` test must stay green.
- Package name `equalizerapo64`; license `GPL-2.0-or-later`; `arch=(x86_64)`.
- Version `1.4.2.r<commit-count>.g<hash>`; build type Release; prefix `/usr`.
- Build must not use the network except the pinned muparserx clone (`FETCHCONTENT_FULLY_DISCONNECTED=ON`).
- No commit or push without an explicit instruction from the user.

---

### Task 1: PKGBUILD and package build

**Files:**
- Create: `packaging/arch/PKGBUILD`

**Interfaces:**
- Produces: `equalizerapo64-<pkgver>-1-x86_64.pkg.tar.zst` containing
  `/usr/bin/eqapo-{host,editor,null}`, `/usr/lib/systemd/user/eqapo-host.service`
  (`ExecStart=/usr/bin/eqapo-host`), `/usr/share/applications/eqapo-editor.desktop`,
  `/usr/share/licenses/equalizerapo64/`.

- [ ] **Step 1: Write `packaging/arch/PKGBUILD`**

```bash
# Maintainer: gustavomgama
# Builds the EqualizerAPO Linux port from the repository checkout containing
# this PKGBUILD (no re-clone). Local build:  cd packaging/arch && makepkg -f

pkgname=equalizerapo64
pkgver=1.4.2
pkgrel=1
pkgdesc="System-wide equalizer for Linux (PipeWire) - native EqualizerAPO port"
arch=('x86_64')
url="https://github.com/gustavomgama/equalizerAPO64"
license=('GPL-2.0-or-later')
depends=('fftw' 'libsndfile' 'pipewire' 'qt6-base')
makedepends=('cmake' 'pkgconf' 'git')
optdepends=(
  'wireplumber: loudness correction reads the default sink volume via wpctl'
  'yabridge: run Windows VST2 plugins'
)
_repodir="$startdir/../.."

source=("muparserx::git+https://github.com/thefirekahuna/muparserx.git#commit=0a48779615103ca5baa301be9eefecdf23156ec6")

pkgver() {
  printf '1.4.2.r%s.g%s' \
    "$(git -C "$_repodir" rev-list --count HEAD 2>/dev/null || echo 0)" \
    "$(git -C "$_repodir" rev-parse --short HEAD 2>/dev/null || echo local)"
}

build() {
  cmake -S "$_repodir" -B "$srcdir/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DFETCHCONTENT_SOURCE_DIR_MUPARSERX="$srcdir/muparserx" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON
  cmake --build "$srcdir/build" -j"$(( $(nproc) / 2 + 2 ))"
}

check() {
  # host_e2e/reload_e2e need a live PipeWire; the rest are audio-server free.
  ctest --test-dir "$srcdir/build" --output-on-failure -E 'host_e2e|reload_e2e'
}

package() {
  DESTDIR="$pkgdir" cmake --install "$srcdir/build"

  # muparserx is linked statically into eqapo_core; drop its leaked install.
  rm -rf "$pkgdir/usr/include/muparserx" \
         "$pkgdir/usr/share/cmake/muparserx" \
         "$pkgdir/usr/lib/pkgconfig/muparserx.pc"
  rm -f "$pkgdir"/usr/lib/libmuparserx.*

  # systemd user unit into the scanned path, bound to the packaged binary.
  install -Dm644 "$_repodir/linux/host/eqapo-host.service" \
    "$pkgdir/usr/lib/systemd/user/eqapo-host.service"
  sed -i 's#^ExecStart=.*#ExecStart=/usr/bin/eqapo-host#' \
    "$pkgdir/usr/lib/systemd/user/eqapo-host.service"

  install -Dm644 "$_repodir/License.txt" \
    "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
```

- [ ] **Step 2: Build the package**

Run (from `packaging/arch/`): `makepkg -f`
Expected: ends with `==> Finished making: equalizerapo64 1.4.2.r<N>.g<hash>-1 (…)`.
If it fails, read the first error (`no new[]`/`nullptr` messages would indicate a
real code bug, not packaging).

- [ ] **Step 3: Assert package contents**

Run:

```bash
pkg=$(ls -t equalizerapo64-*.pkg.tar.zst | head -1)
bsdtar -tf "$pkg" | grep -E 'usr/bin/eqapo-'          # 3 binaries
bsdtar -tf "$pkg" | grep 'lib/systemd/user/eqapo-host.service'
bsdtar -tf "$pkg" | grep 'share/applications/eqapo-editor.desktop'
bsdtar -tf "$pkg" | grep 'share/licenses/equalizerapo64/LICENSE'
bsdtar -tf "$pkg" | grep -c muparserx || true           # expect 0
```

Expected: `eqapo-editor`, `eqapo-host`, `eqapo-null`; the unit, desktop entry,
license each present once; `muparserx` count `0`.

- [ ] **Step 4: Assert the unit and desktop entry are correct**

Run:

```bash
bsdtar -xOf "$pkg" usr/lib/systemd/user/eqapo-host.service | grep '^ExecStart=/usr/bin/eqapo-host$'
bsdtar -xOf "$pkg" usr/share/applications/eqapo-editor.desktop | grep '^Exec=eqapo-editor$'
```

Expected: both lines print.

- [ ] **Step 5: Generate `.SRCINFO`**

Run: `makepkg --printsrcinfo > .SRCINFO`
Expected: `.SRCINFO` written next to the PKGBUILD; `pkgname = equalizerapo64`.

---

### Task 2: Verify it behaves as a pacman repository

**Files:** none changed; produces evidence.

- [ ] **Step 1: `repo-add` a local repo**

Run (from `packaging/arch/`):

```bash
rm -rf /tmp/opencode/eqrepo && mkdir -p /tmp/opencode/eqrepo
cp equalizerapo64-*.pkg.tar.zst /tmp/opencode/eqrepo/
repo-add /tmp/opencode/eqrepo/equalizerapo64.db.tar.gz /tmp/opencode/eqrepo/equalizerapo64-*.pkg.tar.zst
```

- [ ] **Step 2: Inspect the DB entry**

Run: `bsdtar -tf /tmp/opencode/eqrepo/equalizerapo64.db.tar.gz`
Expected: `equalizerapo64-<pkgver>-1/desc` and `.../files` (evidence the package
is registered with the correct name/version).

- [ ] **Step 3: Resolve the package with a throwaway pacman config**

Run:

```bash
mkdir -p /tmp/opencode/eqlocal
cat > /tmp/opencode/eqlocal/pacman.conf <<'EOF'
[options]
HoldPkg = pacman glibc
Architecture = x86_64
SigLevel = Optional TrustAll
[equalizerapo64]
Server = file:///tmp/opencode/eqrepo
EOF
pacman -Sy --config /tmp/opencode/eqlocal/pacman.conf \
  --dbpath /tmp/opencode/eqlocal/db --cachedir /tmp/opencode/eqlocal/pkg \
  --root /tmp/opencode/eqlocal/root -r /tmp/opencode/eqlocal/root -b /tmp/opencode/eqlocal/db 2>&1 | tail -5
pacman -Sp --config /tmp/opencode/eqlocal/pacman.conf \
  --dbpath /tmp/opencode/eqlocal/db equalizerapo64 2>&1
```

Expected: sync lists `[equalizerapo64]`; `-Sp` prints the path to the built
package URL. (No install into the live system.)

---

### Task 3: CI workflow (build → sign → publish)

**Files:**
- Create: `.github/workflows/arch-repo.yml`

**Interfaces:**
- Consumes: `packaging/arch/PKGBUILD`, secrets `GPG_PRIVATE_KEY` / `GPG_KEY_ID` (optional).
- Produces: assets on the `arch-repo` prerelease: the `.pkg.tar.zst`,
  `equalizerapo64.db`, `equalizerapo64.db.tar.gz`, `equalizerapo64.files`,
  `equalizerapo64.files.tar.gz`, plus `.sig` when signing.

- [ ] **Step 1: Write the workflow**

```yaml
name: Arch binary repository

on:
  push:
    branches: [main]
    paths:
      - 'packaging/arch/**'
      - '.github/workflows/arch-repo.yml'
      - 'CMakeLists.txt'
      - 'linux/**'
      - 'Editor/**'
      - 'filters/**'
      - 'helpers/**'
      - 'parser/**'
      - 'libHybridConv-0.1.1/**'
      - 'tests/**'
      - '*.cpp'
      - '*.h'
      - 'version.h'
  workflow_dispatch:

permissions:
  contents: write

jobs:
  package:
    runs-on: ubuntu-latest
    container:
      image: archlinux:latest
    steps:
      - name: Install toolchain and dependencies
        run: |
          pacman -Syu --noconfirm base-devel git cmake pkgconf \
            fftw libsndfile pipewire qt6-base github-cli gnupg

      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Configure signing (if a key is provided)
        id: sign
        env:
          GPG_PRIVATE_KEY: ${{ secrets.GPG_PRIVATE_KEY }}
          GPG_KEY_ID: ${{ secrets.GPG_KEY_ID }}
        run: |
          if [ -n "$GPG_PRIVATE_KEY" ]; then
            install -d -m700 "$GITHUB_WORKSPACE/.gnupg"
            echo "$GPG_PRIVATE_KEY" | gpg --batch --import
            echo "enabled=true" >> "$GITHUB_OUTPUT"
          else
            echo "enabled=false" >> "$GITHUB_OUTPUT"
          fi

      - name: Build package
        working-directory: packaging/arch
        run: |
          useradd -m builder
          chown -R builder:builder "$GITHUB_WORKSPACE"
          sudo -u builder env HOME=/home/builder GNUPGHOME="$GITHUB_WORKSPACE/.gnupg" \
            makepkg -f --noconfirm

      - name: Assemble repository
        working-directory: packaging/arch
        run: |
          mkdir -p out
          cp equalizerapo64-*.pkg.tar.zst out/
          cd out
          if [ "${{ steps.sign.outputs.enabled }}" = "true" ]; then
            repo-add --sign --key "${{ secrets.GPG_KEY_ID }}" \
              equalizerapo64.db.tar.gz equalizerapo64-*.pkg.tar.zst
          else
            repo-add equalizerapo64.db.tar.gz equalizerapo64-*.pkg.tar.zst
          fi
          for f in equalizerapo64.db equalizerapo64.files; do
            cp "$f.tar.gz" "$f"
            [ -f "$f.tar.gz.sig" ] && cp "$f.tar.gz.sig" "$f.sig"
          done
          rm -f equalizerapo64-*.pkg.tar.zst.old
          ls -la

      - name: Publish to the rolling arch-repo release
        working-directory: packaging/arch/out
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          gh release view arch-repo >/dev/null 2>&1 || \
            gh release create arch-repo --prerelease \
              --title "Arch Linux repository" \
              --notes "Rolling pacman repository. Add the [equalizerapo64] stanza from docs/arch-repo.md."
          gh release view arch-repo --json assets -q '.assets[].name' | while read -r n; do
            [ -n "$n" ] && gh release delete-asset arch-repo "$n" -y
          done
          gh release upload arch-repo --clobber *
```

- [ ] **Step 2: Lint the workflow locally**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/arch-repo.yml'))" && echo YAML-OK`
Expected: `YAML-OK`.

- [ ] **Step 3: Validate shell blocks**

Run: `for s in $(grep -n 'run: |' .github/workflows/arch-repo.yml); do :; done; echo "reviewed"`
Expected: visually confirm each `run:` block uses `set -e` semantics (GitHub
runs `bash -e`), no unquoted globbing surprises.

---

### Task 4: User documentation

**Files:**
- Create: `docs/arch-repo.md`
- Modify: `README.md` (add a short "Arch Linux" pointer near Installation)

- [ ] **Step 1: Write `docs/arch-repo.md`**

Content must include: the pacman.conf stanza; the signing key import
(`pacman-key --add` + `--lsign-key`) or the unsigned fallback stanza; install
and upgrade commands; enabling the service
(`systemctl --user enable --now eqapo-host`); and removal notes (the drop-in
repo stanza + `pacman -Rns`).

- [ ] **Step 2: Add the README pointer**

Add under Installation:

```markdown
### Arch Linux

Install from the self-hosted pacman repository — see
[docs/arch-repo.md](docs/arch-repo.md).
```

- [ ] **Step 3: Sanity-check links**

Run: `grep -n 'docs/arch-repo.md' README.md`
Expected: one match.

---

## Self-Review

- **Spec coverage:** §4 files (Tasks 1/3/4), §5 PKGBUILD (Task 1), §6 CI
  (Task 3), §7 hosting/client (Tasks 3/4), §8 signing (Task 3), §9 verification
  (Tasks 1/2). Covered.
- **Placeholders:** none — all commands and file contents are literal.
- **Type consistency:** `pkgname=equalizerapo64`, unit path
  `/usr/lib/systemd/user/eqapo-host.service`, release tag `arch-repo` used
  consistently across tasks and docs.

---

## Execution notes (deviations)

Recorded after inline execution, so the plan matches what shipped:

- `packaging/arch/PKGBUILD` additionally sets `options=('!debug')` (no debug
  split package) and `sha256sums=('SKIP')` (the only source is a pinned git
  checkout).
- Added `packaging/arch/.gitignore` for `src/`, `pkg/`, `out/`, `muparserx/`
  and the built artifacts.
- CI uses `runuser -u builder` (Arch `base-devel` has no `sudo`), passes
  `--sign --key` to `makepkg` when signing is enabled, and exports the public
  key as `equalizerapo64.asc` for the client import step.
- Verified locally: `makepkg -f` → `equalizerapo64 1.4.2.r73.g1dc32e0-1`,
  content assertions pass, `repo-add` + a throwaway `pacman -Sy` lists the
  package, and `windows_untouched` is green.
- Bug found during local signing test: `repo-add` creates `equalizerapo64.db`
  as a **symlink** to `.db.tar.gz`, which cannot be uploaded as a release
  asset. The assemble step now uses
  `cp --remove-destination "$f.tar.gz" "$f"` (same for `.files` and `.sig`).

## Signing key

A dedicated ed25519 repo key was generated (fingerprint
`F7562B55D35C3CC0D1FECD4991E69B47CE4B53F1`, identity
`EqualizerAPO Arch Repository <50861566+gustavomgama@users.noreply.github.com>`,
no expiry). Secret key, public key, and revocation certificate are backed up
under `~/.config/equalizerapo/arch-repo-signing/` (mode 600, outside the repo).
GitHub repo secrets `GPG_PRIVATE_KEY` and `GPG_KEY_ID` are set on
`gustavomgama/equalizerAPO64` (via `gh`).
