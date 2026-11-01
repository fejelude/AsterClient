# Repository stability review — 2026-11-01

Reviewed baseline: `9aba9364481bdea9a3fd271e2965fcd31a0c9616` (`main`).

This review covers the application, UI, module definitions, scheduler, profile storage, Windows platform services, native bridge and protocol, licensing/update client, owner service, tests, packaging and dependency bootstrap. Vendored libraries were checked against the existing lock rather than rewritten. Module names, setting IDs, default values, UI layout and schema version 3 are retained.

## Findings and changes

| Area | Root cause and change | Verification in this review |
|---|---|---|
| Integer configuration | JSON numeric conversions truncated fractions or wrapped integers before validation. Schema `1.5` became `1`; `4294967299` became schema `3`. Keys could wrap into valid virtual-key codes. Integer type/range checks now happen before conversion. | Original defects reproduced; portable regression tests pass. |
| Nested configuration | Arrays/scalars in `license`, `updates`, `bindings` or module `profiles` could silently be ignored. Known sections now require objects; fixed-size arrays require exact lengths and representable elements. | Original malformed-section behavior reproduced; round-trip, migration, malformed input and boundary tests pass. |
| Setting validation | A non-finite module number was reset without a warning. Recovery now reports the affected setting. | Portable regression passes. |
| S-Tap key restoration | Normal completion restored the current forward mapping instead of the key actually suppressed. Changing the map during a tap could leave the original physical key suppressed. Restoration now follows the recorded key. | Original defect reproduced; remapping and release regressions pass. |
| Input cancellation | `tick()` transfers key ownership before dispatch. If dispatch stopped before a queued key-up, a later `stop()` no longer knew that key needed release. `cancelBatch()` retains pending cleanup releases and respects keys now physically held. Both mid-batch interruption and send-failure paths use it. | Portable cancellation regression passes; actual Windows input dispatch needs the manual checks below. |
| Bridge liveness | Successful telemetry reads refreshed readiness, but a failed read could leave cached readiness indefinitely. Liveness is now recomputed from the host clock on each status request; at 500 ms, world permission, targets and applied states expire. Disconnect/process-change paths invalidate cached state before waiting for shutdown. | Portable liveness/status boundary tests pass. Real game exit/restart remains untested here. |
| Telemetry strings | The host assumed the shared `screen` array was terminated. Reads now stop at its fixed array boundary. | Code review; native transport testing pending. |
| Autosave while hidden | Autosave ran only in `render()` and used ImGui frame time, which stops advancing when hidden/minimized. A main-loop maintenance tick now uses the steady clock, preserving debounce and retry intervals. | Code review; Windows hidden-window test pending. |
| Imported backgrounds | Import retained the previous image bytes. It now reloads the imported path or clears the image. Pending downloads are generation-checked so an old result cannot override an imported profile or Use Contours. | Code review; Windows decoder/UI tests pending. |
| Resize lifecycle | The device context could retain the old render-target binding when buffers were resized. It is explicitly unbound before release/resize. | Code review; Windows resize/device-loss tests pending. |
| Profile file names | Temporary/recovery names used only process ID and millisecond tick count. Multiple operations in one tick could collide. A process-local atomic sequence now makes them unique. | Code review; Windows filesystem tests pending. |
| Remembered license sessions | A fresh activation first parsed the old vault record, so a corrupt cache blocked replacement. Activation now verifies the new signed response independently. | Windows native integration regression added; not executed here. |
| Service validation | RegExp coercion accepted an array containing a nonce; prefix matching accepted `application/jsonp`. Both now require the intended type/media type. | Loopback service regressions pass. |
| Service expiry | Cleanup used `< now`, allowing a refresh token at its exact expiry second. Expired rows now use `<= now`. Failed transactions still roll back nonce reservations. | Loopback expiry/rollback regression passes. |
| Service startup resources | Reading signing/TLS files after opening SQLite, or a failed listen, could retain the database handle. Files are read first; construction/listen errors close the database once. Partial TLS configuration is rejected. | Code review; service happy-path lifecycle passes. Additional TLS/listen fault injection remains useful. |
| Dependency bootstrap | Hash verification occurred after downloads were written and archives extracted. Each fetched file/archive is now checked against the existing lock before persistence/use; bootstrap no longer rewrites the lock to accept new bytes. | Four offline download/cache regressions pass; all 112 vendored files match their pinned hashes. Full network bootstrap not run. |
| Build/test structure | Windows-only imports prevented configuration/scheduler tests elsewhere. Profile conversion and model/planner math now form `aster_model`, with a portable test-only CMake option. Native builds continue linking the same implementation. Core-only builds skip ImGui; MSVC ASan applies before the core-only return and to test executables. Preset schema 5 matches the stated CMake 3.24 minimum. | Portable CMake Release and sanitizer builds pass. Windows build configurations await CI/manual execution. |
| Test environment handling | Service tests always spawned `AsterTests.exe` and left private fixtures behind. Non-Windows/missing-executable cases now report explicit skips, fixtures are removed, and `ASTER_REQUIRE_NATIVE_TESTS=1` makes missing native coverage fail CI. | Service checks pass with three explicit native skips on Linux. |

## Results actually obtained

Host: Linux x64, GCC 13.3, CMake 3.31.10, Node 24.22.1. No Windows desktop, MSVC/MASM, Windows SDK or running Minecraft was available.

| Check | Result |
|---|---|
| Portable CMake Release build, `-Wall -Wextra -Wpedantic` | Passed, no compiler warnings. |
| `AsterCoreTests` / CTest | 16 cases passed. Covers all module/profile round-trips, migration, invalid configurations, key cleanup, scheduling, telemetry expiry, health reporting and math. |
| Same suite with GCC AddressSanitizer + UndefinedBehaviorSanitizer | 16 passed. LeakSanitizer disabled because this host runs under ptrace; this is not a leak-test result. |
| `node --test service/tests.mjs` | 11 service subtests passed, plus the parent suite; three Windows native integration subtests skipped with reasons. |
| Python dependency bootstrap regressions | 4 passed. |
| `python tools/bootstrap.py --verify-only` | 112 pinned files verified, no downloads or changes. |
| Service and Bedrock QA JavaScript syntax checks | Passed. This does not execute Minecraft's script API. |
| Original-code reproduction harness | Reproduced five defects: fractional schema, wrapped schema, ignored license section, ignored module profile section, wrong S-Tap restore key. Harness was temporary and is not shipped. |
| `git diff --check` | Passed. |
| Windows executable, DLL, native tests and packaging | Not run locally. The repository workflow builds, runs CTest and required native service tests, packages, and smoke-tests the installed application on Windows. Check its actual result on the PR. |
| Live Minecraft module effects, CPU/memory usage and game restart | Not measured in this review. Historical measurements in `VALIDATION.md` describe the earlier snapshot and do not validate this patch. |

## Compatibility and remaining work

The existing allowlist remains Store x64 build `1.26.6005.0`, SHA-256 `988f6a61403b4381aa41e6c66d64cce441b14e561a9b10f647c2758ef3f2672a`. There is no evidence here for a newer Bedrock version. No gameplay module was removed or renamed; no new game modification, bypass or concealment mechanism was added.

The existing integration loads a bridge into Minecraft and uses native detours and exact-build memory layouts. This review does not establish authorization for any other process or environment. The existing bridge has incremental hook initialization, shared callback state and complex teardown paths that need native failure/concurrency testing. Changing those mechanisms without a game test environment would require a separate, measurable native-runtime change.

Other remaining limitations:

- A transient adapter connection failure still requires restarting Minecraft before another attempt. Automatic retries must distinguish safe failures from a pending remote loader; retrying blindly could create duplicate bridges.
- The existing loader retains its remote argument after a timeout so a pending loader cannot dereference freed memory. A bounded, process-aware pending-load lifecycle needs Windows testing.
- The shared-memory consistency protocol and callback teardown need thread/race stress tests. The portable tests do not prove cross-process memory ordering or unload safety.
- Server acceptance, physical input timing, camera/timer restoration and per-module performance require the exact supported binary in a disposable, explicitly authorized world.
- Signed update acceptance requires the owner's trusted signing setup. This review does not provision a service, release certificate or newer Minecraft adapter.

## Windows and private-world verification checklist

1. Build all targets, run CTest and required native service tests, package, then launch the installed EXE beside its DLL. Check the workflow result and artifacts rather than treating workflow creation as a successful Windows build.
2. Change a setting, immediately hide/minimize, wait two seconds, and read the profile from disk before restoring the window. Repeat with a module hotkey while the client is hidden, then restart and confirm persistence.
3. Import a profile with a different image and one with no image. Start a slow image download, then select Use Contours or import a profile; the older result must not replace the selection. Test missing/corrupt images and device recreation.
4. Resize repeatedly and move between DPI scales. Check responsiveness and confirm resizing does not repeatedly recreate the renderer or stop input unexpectedly.
5. Exercise corrupt/default/backup/future-schema recovery and repeated recovery attempts; confirm original bytes remain preserved. Verify the new corrupt-vault activation regression.
6. In a disposable authorized world, exercise every module's settings and both Aim profiles. Test F8/F9, chat/menu/capture loss, physical W/S ownership, custom/duplicate movement maps, map changes during a tap, and send failure while a release is queued.
7. Close and restart Minecraft while armed, switch worlds, and stall/disconnect telemetry. The host must lose world readiness and require an explicit new arm. Unsupported executable identities must remain refused.
8. Measure idle/armed CPU, memory over time, frame responsiveness and host/game-thread timing. Separate native dispatch counters, observed component state and accepted world events in the result.
