# Compatibility

The native adapter supports exactly Microsoft Store x64 package `Microsoft.MinecraftUWP_8wekyb3d8bbwe`, build `1.26.6005.0`, executable SHA-256 `988f6a61403b4381aa41e6c66d64cce441b14e561a9b10f647c2758ef3f2672a`. Observed game protocol: 2225. Package identity, architecture, version and executable hash must all match.

Development host: Windows 11 x64 build 26200, Visual Studio 2022/MSVC v143, Windows SDK 10.0.22621. No administrator privileges or security changes are part of normal operation.

| Capability | Exact supported binary | Other binaries |
|---|---|---|
| Native UI, profiles, appearance, diagnostics | Available independently of gameplay | Available independently of gameplay |
| Package/version detection | Verified through Windows package APIs | Detection only |
| Reach / targeting Hitbox | Native ray query and dispatch, accepted private-world hit/damage tests | Refused |
| Aim Assist | Qualified input-camera steering, independent axes and condition tests | Refused |
| Velocity | Local incoming-motion receive binding and observed component/trajectory tests | Refused |
| Timer | Local clock field ownership/restoration, measured actor ticks | Refused |
| Zoom / Auto 360 | Tagged camera FOV and yaw, live observations | Refused |
| Clicker / contextual mining | Native handlers; separate schedule and game counters | Refused |
| Walk / Sprint / S-Tap | Foreground/world-gated configured keys; see validation | Refused |
| Own license/vault, signed metadata | Independent of game | Independent of game |

Read [validation](VALIDATION.md) for actual coverage. A bound handler does not promise server acceptance. Local Timer does not control server TPS. Server authority can correct motion. LOS conservatively treats non-fluid block voxels as occluders, including glass and partial blocks.

Other versions require a researched port, an updated exact hash/version allowlist and renewed testing. Historical SDK references do not establish broad support. No kernel component, anti-cheat bypass or copied Nitro implementation is included.
