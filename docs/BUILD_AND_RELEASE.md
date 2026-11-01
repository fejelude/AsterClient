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

## Offline and portable validation

Vendored dependency source is included. To validate it without downloading CMake or SDK archives:

```sh
python tools/bootstrap.py --verify-only
python -m unittest discover -s tests -p '*_tests.py' -v
cmake -S . -B build-portable -DASTER_PORTABLE_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-portable --parallel 2
ctest --test-dir build-portable --output-on-failure
node --test service/tests.mjs
```

The portable target builds model, profile conversion, math and scheduler tests only. It produces no Windows client or bridge. Native service integration tests explicitly skip when Windows/the executable is unavailable. On Windows, use `ASTER_NATIVE_TESTS_EXE` for a non-default test binary path, and set `ASTER_REQUIRE_NATIVE_TESTS=1` to reject missing native coverage.

Bootstrap consumes the reviewed `dependencies.lock.json`; dependency upgrades require explicitly reviewing and updating pins. Hash failures preserve existing files and stop before writing or extracting an unverified download.

`.github/workflows/validate.yml` runs portable checks and a separate Windows build/CTest/native-service/package/startup job. The Windows job uploads package archives and test-result summaries. A workflow configuration is not evidence of a passing run; consult the PR's actual checks. See [the stability review](REPOSITORY_REVIEW.md) for manual checks and results from this patch.
