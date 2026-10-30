# Implementation status

This is an original implementation guided by the 27-page specification supplied on October 30, 2026. The reference screenshots and Nitro proprietary artwork are not distributed as application assets.

## Verified development environment

- Windows x64; Visual Studio 2022 Community 17.14, MSVC 14.44, Windows SDK 10.0.22621.
- Installed and running Minecraft Bedrock package: `Microsoft.MinecraftUWP`, x64, build `1.26.6005.0`.
- Dear ImGui 1.91.9b and nlohmann/json 3.12.0 pinned by commit and downloaded-file SHA-256.
- Portable CMake is local to this project. No administrator rights or system installation is required.

## Engineering boundary

The PDF explicitly does not supply verified engine integration for Reach, Aim Assist, Hitbox, Velocity, Timer, Zoom, or Auto 360. Its modern reference SDK targets 26.52, not the installed build. No engine offsets from an unverified build will be executed. A setting marked unsupported is a saved configuration request, never a claim that the game changed.

Native input acceptance tests are distinct from game effect verification. Windows accepting a SendInput request is not an acknowledgment from Minecraft. Candidate input features will remain unverified for gameplay until controlled in-world tests pass.

## Work streams

1. Reproducible native shell: Win32, Direct3D 11, Dear ImGui.
2. Typed profiles, migrations, atomic persistence, per-module lifecycle and honest health.
3. Game identity/version/foreground detection and focus-safe physical input observation.
4. Independent click scheduling, configurable movement keys and explicit capability gates.
5. Original animated appearance and all observed control groups with clear unknown semantics.
6. Independent license service and Windows Credential Manager persistence.
7. Automated core/platform tests, native UI checks, portable release, hashes and documentation.

## Release gates

G5 (every game module verified in-world) and trusted commercial code signing require evidence or resources beyond the supplied PDF. They must not be reported as passed on the strength of a GUI or unit tests.
