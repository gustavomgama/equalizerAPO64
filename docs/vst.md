# VST plugins on Linux

`VSTPlugin: Library <path>` accepts native and Windows plugins. Windows
binaries are never loaded directly (Linux cannot execute PE files); they are
mapped to a **yabridge** (Wine) wrapper. Native VST3 hosting is being added —
until it lands, `.vst3` files resolve but report as pending.

## Supported

| Path | What happens |
|---|---|
| `plugin.so` (Linux VST2) | Loaded via `dlopen` today. Relative paths resolve under `~/.vst`. |
| `Plugin.dll` (Windows VST2) | Mapped to `~/.vst/yabridge/<name>.so` if present, else the log prints the exact `yabridgectl add` command. |
| `Plugin.vst3` (VST3) | Resolved (bundle or file). Native VST3 hosting is in progress; currently reports as pending and the filter passes audio through. |

## Setting up Windows plugins

1. Install Wine and yabridge:
   `sudo pacman -S wine` and install `yabridge`/`yabridge-bin` from the AUR.
2. Register the folder holding the Windows plugins (the host's log prints this
   exact command when it sees an unmapped one):
   `bash packaging/arch/yabridge-setup.sh "/path/to/VSTPlugins"`
3. Keep the Windows path in `config.txt`; the host finds the wrapper. A direct
   reference to the wrapper (`~/.vst/yabridge/Foo.so`) always works too.
4. The plugin drive must be mounted and readable (a missing file reports
   "not found", not a bridge error).

## Notes

- A crashing native plugin takes down `eqapo-host`; systemd restarts it.
  Treat third-party plugins as trusted code.
- VST3 parameter automation and state use the same `VSTPlugin:` `ChunkData`
  and named-parameter schema as VST2, so configs carry over.
