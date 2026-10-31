# Build and portable package

Use Windows x64, Visual Studio 2022 Desktop development with C++ (v143), Windows SDK 10.0.22621, and Python 3.10+ for bootstrap. Node 24+ is needed only for optional service tests. The application has no Python/Node runtime dependency.

```powershell
python tools/bootstrap.py
$cmake = '.\tools\vendor\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
& $cmake --preset windows-x64
& $cmake --build --preset release --parallel 4
& '.\tools\vendor\cmake-3.31.8-windows-x86_64\bin\ctest.exe' --preset release
node --test service/tests.mjs
.\tools\package.ps1
```

Bootstrap downloads pinned dependencies into this project without a system installation. Keep `AsterClient.exe` and `AsterBridge.dll` together. The packaging script installs into a new timestamped directory under `dist`, writes `SHA256SUMS.txt`, creates a ZIP and writes its `.sha256` sidecar. It refuses to overwrite an existing package. Install includes both binaries, documentation, the dependency lock, license notices and optional owner-service runtime/guide. AsterBridgeTool and the QA pack are developer tools, not automatic game installations.

Before publishing, review runtime coverage, run the regression checks, inspect the installed package, verify its SHA-256 manifest and launch it with a disposable profile. Exclude research, test results, game binaries/worlds and credentials. The development build is unsigned. Trusted update acceptance requires valid Authenticode signing and the configured publisher certificate hash; security settings must not be changed to bypass failure. Documentation under `docs/` is delivered; private material under `docs/spec-review/` is excluded.

Isolated development options: `--profile <path>`, `--page <category> <tab>`, `--smoke-seconds <1..60>`, `--diagnose <output.json>`. An isolated profile preserves the user's default configuration during UI tests.
