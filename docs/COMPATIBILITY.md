# Compatibility matrix

Development evidence: Windows 11 Home Single Language, x64, build 26200. Minecraft Store package `Microsoft.MinecraftUWP_8wekyb3d8bbwe`, x64, `1.26.6005.0` was running and positively identified through Windows package APIs. The world-selection screen was inspected without opening or changing a user world.

| Capability | 1.26.6005.0 | Other/future builds | Evidence |
|---|---|---|---|
| Native UI, profiles, appearance, diagnostics | Available independently of game | Available | Built, executed, native UI inspected |
| Minecraft identity/version/architecture detection | Verified on this PC | Identity can be detected; no support inferred | Live package API and native test |
| Foreground/cursor metadata | Implemented | Metadata only | Native window fixture and live menu report |
| Click scheduling, priming, jitter, inversion | Windows fixture verified; **game effect unverified** | Input denied | Real SendInput fixture plus deterministic scheduler tests |
| Sprint and Auto Walk | Key output/cleanup verified in fixture; **game effect unverified** | Input denied | Owned-key and cancellation tests |
| Reach, Aim Assist, Hitbox, Velocity, Timer, Zoom, Auto 360 | **Unsupported** | **Unsupported** | No verified game-engine adapter |
| Auto S-Tap, Tuner, Triggerbot, block-breaking modes | **Unsupported / behavior undefined** | **Unsupported** | PDF explicitly leaves behavior or required telemetry unknown |
| License/vault | Verified against included own-service fixture | Independent of game | Activation, restart, grace, corruption and revocation tests |
| Signed update metadata | Verified against included own-service fixture | Independent of game | Positive signature and altered-envelope rejection |
| Trusted signed package update | Verification implemented; positive public-release path not tested | Independent of game | No trusted signing certificate or hosted release supplied |

The compiled input allowlist contains only the named research build. Its status is partial/unverified, not Connected or Active. Any future build is unverified until a source change and renewed testing explicitly allow it. The engine capability set is empty.

No supplied material establishes safe current engine layouts or exact proprietary behavior. The modern ModBase reference explicitly targets 26.52, and the archived Horion SDK is from 2021. No offsets, signatures or code from those projects are executed in Minecraft. Broader source research did not establish a verified 26.60 engine adapter suitable for this clean-room implementation.

Physical-device tests at swapped-button settings, game-world input acknowledgment, supported engine module effects, GPU/vendor combinations, actual driver removal, reboot, fresh user/clean VM, and 100/125/150/200% Windows DPI are **not** established by the native fixture. See VERIFICATION.md; the app does not claim these tests passed.
