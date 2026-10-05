# Release It Review — Linux Port

Review of the EqualizerAPO Linux port against the Release It! policy
(`~/.opencode/skills/release-it`). Production-readiness findings and their
disposition.

## Findings and fixes

| # | Finding | Rule | Disposition |
|---|---|---|---|
| 1 | Ring buffer overwrote unread audio silently when the producer outpaced the consumer | Bounded queues / back-pressure | **Fixed**: bounded buffer with explicit drop-oldest policy; RT thread never blocks; `overruns` counted. |
| 2 | PipeWire stream errors were ignored; the host could stall silently | Fail fast, restartable | **Fixed**: `state_changed` callbacks log every transition; `PW_STREAM_STATE_ERROR` sets a failure flag and exits non-zero. |
| 3 | No runtime metrics for audio over/underruns | Observability | **Fixed**: non-RT stats thread logs over/underrun counters every 5 s when they change; totals logged on shutdown. |
| 4 | No restart policy or service definition | Restartable automation | **Fixed**: `linux/host/eqapo-host.service` with `Restart=on-failure`, `RestartSec=2`, and a `StartLimitBurst` cap to prevent crash loops. |
| 5 | `eqapo-null` ignored write errors | Failure paths | **Fixed**: checks `sf_writef_double` and returns non-zero with the libsndfile error. |
| 6 | A crashing native VST plugin takes down the host process on Linux | Blast radius | **Documented limitation** (see below). |

## Known limitation: VST crash blast radius

On Windows the VST host wraps plugin calls in SEH (`__try`/`__except`) and
degrades to passthrough on a crash. Linux has no equivalent in-process
protection, so a segfaulting native VST plugin crashes `eqapo-host` (and the
systemd unit restarts it). This is the honest trade-off of native in-process
hosting.

Options, in increasing cost:
1. **Restart isolation (current):** systemd restarts the host; audio drops for
   ~2 s. Acceptable for a misbehaving plugin.
2. **Config-level disable:** remove the `VSTPlugin:` line and reload.
3. **Out-of-process hosting:** run each plugin in a child process and pass
   audio over a pipe. Isolates fully; significant work; deferred.

Until (3), treat third-party VST plugins as trusted code.

## Deliberately not applied

Release It's distributed-systems patterns (circuit breakers, retry with
jitter, dead-letter queues, health endpoints) target network services. This
component has one local dependency (PipeWire) and no remote calls, so those
rules do not apply. The applicable subset — bounded buffers, fail-fast,
observability, restartable deployment — is implemented above.
