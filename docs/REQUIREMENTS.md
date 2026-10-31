# Requirements traceability

The supplied 27-page clean-room specification defines the navigation, controls, appearance and application systems. The 23-page ASTER Bedrock Lab guide adds runtime behavior and evidence requirements. Both were read in full, including the original reference-image atlas. The user's approved independent behavior is defined in [RUNTIME_CONTRACT.md](RUNTIME_CONTRACT.md); actual observations and untested scenarios are in [VALIDATION.md](VALIDATION.md).

## Gameplay settings

| Requirement | Implementation | Verification |
|---|---|---|
| Reach distance, Combat Mode, Only Moving | `src/integration/bridge_dll.cpp`: native ray/AABB targeting, physical combat and motion gates; actual GameMode attack | Extended accepted hits, disabled/3-block baselines, combat/movement gates and solid-wall trials |
| Clicker left/right CPS, Tuner, priming, randomize, inversion | `src/core/planner.cpp`, `src/platform/windows.cpp`: independent monotonic clocks, physical event ordering, polling/timer ownership; bridge dispatches native attacks, interactions, item use and block use | Independent streams, inversion, priming, interval limits and low-rate polling tests; live left-stream native/server counts. Final right-placement path has the coverage limit in validation |
| Triggerbot, Break Blocks Auto/Legit | Native unobstructed entity/block query; planner's contextual attack stream and 250 ms Legit delay; native start/continue/stop mining | Triggerbot live combined tests, one server-confirmed Survival block break; held/released and inverted mining tests |
| Aim Closet/Blatant, Smoothness, both FOV values, Distance, Height | Independently stored profiles; `src/core/engine_math.cpp` and qualified input-camera steering; radians and block units made explicit | Axis isolation, convergence/dead zone, profile round-trip and 15 live phases |
| Aim Vertical/Horizontal, Antibot, Only Moving, Avoid Walls, Avoid On Target, Cache Targets, Combat Mode | Per-axis writes, player-structure filter, common gates, conservative voxel LOS, unexpanded target ray and world/registry/entity identity | Live single-target gate trials plus pure target/geometry tests. Antibot is player-only filtering, not universal NPC recognition |
| Hitbox Width, Deterministic | Absolute virtual horizontal targeting width; preserved height and physical collision; nearest ray/entity-ID ordering or eligible previous target | Off-axis small/large width, combined Reach and wall trials; geometry tests |
| Velocity X/Z, Y, Percentage, Precision, Combat Mode, Combine Modifiers, No Backwards | UI percentage/factor representations; native local SetActorMotion receive callback; independent vector math and borrowed-packet restoration | 50 live controlled trials / 100 observed callbacks, fractional values and zero/full baselines; composition/no-backwards math tests |
| Timer multiplier and Combat Mode | Owned native local TPS field; prior baseline restoration; measured actor tick rate | Live 0.8x/1.1x/1.3x/off and main-controller F9/menu restoration |
| Zoom speed and Low CPU Mode | Owned render-camera FOV; explicit transition duration; lower host polling when compatible with other modules | Observed FOV factor 0.35 and visible restore; main combined session |
| Auto 360 speed, left/right weights, step, interval | Real-time complete yaw turn after an observed attack; direction weights and residual-angle completion; Aim yields during spin | Observed 359.99997-degree turn and one completion; spin math tests |
| Sprint, Auto Walk, Auto S-Tap | Configurable foreground key map, deduplicated synthetic key ownership; observed attack triggers a 60 ms backward tap, with explicit-stop forward restoration | Real in-game Auto Walk/F9/focus tests; planner and Windows input fixture. Complete physical S-Tap/Sprint walkthrough not claimed |

## Application systems

| Requirement | Implementation and result |
|---|---|
| Navigation, module settings and optional binds | Native Win32/Direct3D 11/Dear ImGui UI in `src/ui/interface.cpp`. All gameplay settings map to typed runtime configuration. Bind conflicts are reported; optional binds can be removed. F10/tray restores the window; F8 arms and F9 stops |
| Colors, hue bar, footer, opacity, scaling, DPI, window lock, background image | Saved appearance configuration; WIC image decode, original animated contours, independent panel/texture opacity and image transforms. Imported bitmaps are still images; image speed controls the contours |
| Profiles and saves | `src/config/store.cpp`: auto-save, explicit save, import/export, validation, migration, atomic replacement, backup/corruption recovery, future-schema preservation. Round-trip and failure tests pass |
| Game detection and runtime truth | Windows package/architecture/version/SHA-256 gate; one exact build. Separate configured, bound, requested, applied and observed states. Fresh native capture supports UWP even when Win32 cursor clipping is absent |
| Stop, cleanup and session changes | Host/bridge session, heartbeat and pinned world identity; focus/menu/capture loss requires re-arm. Owned key holds, Timer and FOV release/restore; callbacks drain before unload. Main-controller F9, focus and inventory checks passed |
| License, login, remembered sessions | Original optional owner service, signed nonce-bound entitlements, rotating sessions, revocation and bounded offline grace. Windows Credential Manager keeps secrets outside profile JSON. Service/native integration tests pass; no production service is provisioned |
| Updates | Original signed feed and download verification, hash and pinned trusted Authenticode publisher. Native service-signed metadata test passes. Applying a release is manual; this unsigned developer build is not represented as a trusted signed update |
| Diagnostics and logs | Local bounded structured logs, preview/copy report and secret/path redaction. No automatic telemetry upload; redaction tests pass |
| Anti OBS, Discord and Vouch references | The original specification treats Anti OBS as reference evidence, not a monitoring-hiding requirement. It is explicitly unavailable. Discord/Vouch destinations require owner-provided URLs and are explicitly unavailable; no fake destination or success action is implemented |
| Build, notices and delivery | Pinned dependency hashes and licenses, CMake source build, client + bridge portable package, SHA-256 manifest and optional original QA pack source. See [BUILD_AND_RELEASE.md](BUILD_AND_RELEASE.md) |

Unavailable external infrastructure and unmeasured scenarios are stated explicitly. A reference screenshot or a saved toggle is never used as proof of a native game effect.
