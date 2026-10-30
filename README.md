# Aster Client - native Bedrock client lab

Source upload: see [current snapshot status](docs/SNAPSHOT_STATUS.md) for the experimental bridge work and remaining validation.

An original Windows 11 x64 development build guided by the supplied Nitro clean-room specification. It uses C++20, Win32, Direct3D 11 and Dear ImGui. No Nitro binaries, secrets, branding assets, license endpoints, engine offsets or noncommercial/GPL client code are included.

**This is a tested native development milestone, not a completed Nitro gameplay replacement.** Reach, Aim Assist, Hitbox, Velocity, Timer, Zoom, Auto 360 and Auto S-Tap have no verified game integration and are explicitly marked unsupported. A configured switch never means the game effect was verified.

## Run

Open `AsterClient.exe` from the portable release folder. No installer, administrator privileges, VC runtime installation, game injection or system configuration change is needed. Keep the accompanying documentation and notices with the executable.

- Settings auto-save after editing pauses. `Ctrl+S` and **Plugs > SAVE CONFIG** explicitly save.
- `F10` hides/restores the window by default. The tray icon also restores it. Hide may be unbound or changed.
- `F8`, pressed physically inside Minecraft, arms/disarms experimental input. `F9` stops it. All modules start disabled in a new profile.
- The input session pauses on focus loss, game exit, cursor release and common chat/menu keys. A held mouse button must be released and pressed again after arming.
- Input output is restricted to the verified Store package identity, x64 architecture and research build `1.26.6005.0`. A new game build fails closed.
- The input bridge implements hold-to-repeat left/right CPS, priming, interval randomization, logical click inversion, Auto Walk and Sprint using your movement-key map. Windows input delivery is tested; **Minecraft acceptance and gameplay effects remain unverified**.
- Tuner, Triggerbot and both block-breaking modes are disabled with explanations. Their behavior is not established by the PDF.

Do private-world validation before using experimental input. Open Minecraft's key settings and match the app's movement map. No automatic action starts merely because Minecraft is detected. There is no anti-cheat or screen-capture evasion.

## Data

The default profile is `%APPDATA%\AsterClient\profiles\default.json`. Atomic commits keep one `.bak`; invalid JSON is preserved in a `.corrupt-*` file before recovery. Future schemas are read-only. Background images and bounded local logs live beside the profiles directory. License sessions are stored only in Windows Credential Manager, under `AsterClient/license/<endpoint-and-public-key-hash>`.

Profile import/export includes preferences and public service configuration, never keys or tokens. No telemetry is uploaded automatically. Diagnostics show exactly what Copy Report puts on the clipboard. See [privacy and input behavior](docs/PRIVACY.md).

## Rebuild on Windows x64

Prerequisites: Visual Studio 2022 with Desktop development with C++, MSVC v143 and Windows SDK 10.0.22621; Python 3.10+ for dependency bootstrap; Git is optional for this source delivery. The native executable has no Python or Node runtime dependency. The independent service/tests require Node 24+.

From PowerShell in this directory:

```powershell
python tools/bootstrap.py
$cmake = '.\tools\vendor\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
& $cmake --preset windows-x64
& $cmake --build --preset release --parallel 4
& '.\tools\vendor\cmake-3.31.8-windows-x86_64\bin\ctest.exe' --preset release
node --test service/tests.mjs
```

If `python` is the Windows Store alias, use an installed Python executable explicitly. Bootstrap downloads pinned dependencies into this project and installs nothing system-wide. The generated icon is included in source; regenerating it with `tools/make_icon.py` additionally requires Pillow.

The unmodified upstream reference example is reproducible separately:

```powershell
& $cmake -S . -B build-baseline -G 'Visual Studio 17 2022' -A x64 -DASTER_BOOTSTRAP_ONLY=ON
& $cmake --build build-baseline --config Release --target ImGuiBaseline
```

Build output: `build/Release/AsterClient.exe`. Compiler and dependency details are recorded in the release verification report and `dependencies.lock.json`. See [build, package and release](docs/BUILD_AND_RELEASE.md).

## Own license and update services

The local developer build is available without a purchased key. An independent, owner-operated service is included under `service/`; it provides activation, rotating sessions, revocation, signed offline entitlements and signed release metadata. It does not use Nitro's servers. See [service deployment](service/README.md).

Configure an HTTPS endpoint and its RSA public PEM in **Plugs**, apply the settings, then enter a key from that service. Only explicit loopback development fixtures accept HTTP at `127.0.0.1`. Production TLS verification is never disabled.

The update checker verifies signed metadata, SHA-256 and a trusted Authenticode publisher pin before accepting a downloaded executable. Applying the downloaded release is manual and preserves the prior executable for rollback. No live service, purchase system, hosted update feed or trusted signing certificate is provisioned by this project.

## Verification and limitations

Read [implementation status](docs/IMPLEMENTATION_STATUS.md), [compatibility](docs/COMPATIBILITY.md), [requirements traceability](docs/REQUIREMENTS.md), and [verification report](docs/VERIFICATION.md). The report separates automated tests, live UI checks, Windows input tests and unperformed in-world tests. No supplied material establishes exact proprietary gameplay algorithms; unsupported capability labels are deliberate, not hidden success claims.

The development release is **unsigned**. Verify the published SHA-256 manifest. A trusted publisher certificate is required before a public signed release; no trust-store or antivirus settings are changed.
