# Installing EqualizerAPO on Arch Linux

The Linux port is published as a self-hosted pacman repository, hosted on the
project's GitHub Releases under the rolling `arch-repo` prerelease. Add the
stanza below to `/etc/pacman.conf`, then install with `pacman`.

> The repository builds from `main` and tracks the **latest** build only. It is
> `x86_64` only; the binaries use AVX2/FMA (any modern x86-64 CPU).

## 1. Add the repository

Append to `/etc/pacman.conf` (replace `gustavomgama/equalizerAPO64` if you use
a fork):

```ini
[equalizerapo64]
SigLevel = Required DatabaseRequired
Server = https://github.com/gustavomgama/equalizerAPO64/releases/download/arch-repo
```

### Signing key

The database and packages are signed. Import and locally trust the repository
key once:

```bash
curl -L -o /tmp/equalizerapo64.asc \
  https://github.com/gustavomgama/equalizerAPO64/releases/download/arch-repo/equalizerapo64.asc
sudo pacman-key --add /tmp/equalizerapo64.asc
sudo pacman-key --lsign-key "$(gpg --show-keys --with-colons /tmp/equalizerapo64.asc | awk -F: '/^fpr:/{print $10; exit}')"
```

If the repository is served **unsigned** (no key published), use this stanza
instead — note this is insecure and only skips signature checking, not package
integrity (pacman still verifies checksums against the database):

```ini
[equalizerapo64]
SigLevel = Optional TrustAll
Server = https://github.com/gustavomgama/equalizerAPO64/releases/download/arch-repo
```

## 2. Install, upgrade, remove

```bash
sudo pacman -Sy equalizerapo64        # install
sudo pacman -Syu                      # upgrade with the rest of the system
sudo pacman -Rns equalizerapo64       # remove (then delete the stanza above)
```

Installed commands: `eqapo-host`, `eqapo-editor`, `eqapo-null`.

## 3. Run the system-wide EQ

The package ships a systemd **user** unit but does not enable it (it becomes
the default sink's processor once you route audio to it, so that stays your
choice):

```bash
systemctl --user enable --now eqapo-host.service
systemctl --user status eqapo-host.service
```

This creates a virtual sink named `EqualizerAPO` and hot-reloads
`~/.config/equalizerapo/config.txt`. It deliberately does not change the
system default sink; route applications to `EqualizerAPO` or set it yourself.
The host forwards processed audio to a real hardware sink and never loops into
its own virtual sink, so making `EqualizerAPO` the default output is safe:

```bash
wpctl status                # find the "EqualizerAPO" sink id
wpctl set-default <id>      # route everything through the EQ
wpctl set-default <real-id> # restore
```

Configure the chain with `eqapo-editor`, then save — the host picks it up live.

## Notes and limitations

- **Loudness correction** reads the default sink volume via `wpctl`; install
  `wireplumber` for it (otherwise the value is unavailable).
- **Windows VST2** binaries cannot be loaded directly; use `yabridge` to expose
  them as native `.so`. Native Linux VST2 plugins load as-is.
- A crashing VST plugin takes down `eqapo-host`; systemd restarts it (~2 s of
  dropped audio). Treat third-party plugins as trusted code.
- `Device:` blocks match the output device the host forwards to (the playback
  device listed in the Editor); `EqualizerAPO` also matches, and
  `Device: all` matches everything. Windows device names such as `Speakers` do
  not match.
- See `docs/linux-port-status.md` for the full status and limitation list.
