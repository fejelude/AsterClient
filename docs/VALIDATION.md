# Runtime validation

Development validation on 31 October 2026, Windows 11 x64, Microsoft Store Bedrock package `1.26.6005.0`, protocol 2225. Exact executable SHA-256 is recorded in the runtime contract. Tests use a newly created disposable Survival flat world and an original, optional server-side observer pack. Existing user worlds are not test fixtures.

## Evidence rules

Saved settings, native binding, dispatched requests, observed native state and server events are separate evidence. Native hit callbacks do not prove damage. A zero fault count is necessary but does not by itself pass a scenario. Late or unfocused trials are inconclusive. Raw JSON, local content logs and research remain excluded from source and portable packages.

## Completed measurements

| Feature | Measured result | Scope |
|---|---|---|
| Core/platform | 34 automated cases passed, zero failed in the final release build | Typed configuration, migrations, persistence/recovery, units, geometry, impulse composition, steering, independent CPS, priming, jitter, Triggerbot gates, normal/inverted mining, S-Tap timing/cancellation, key ownership, native capture gating, credentials, diagnostics and signature rejection. A 30 CPS / 50 Hz scheduler test checks that polling jitter does not lower the mean rate or create catch-up bursts |
| Owner service | 13 tests passed, zero failed | TLS policy, signed nonce-bound entitlements, real Windows client activation/vault/session flow, refresh rotation/replay rejection, revocation, expiry, hashed secrets, signed updates and offline grace |
| Windows input fixture | Visible fixture run passed its then-current 33 cases, zero failed | Real Windows input delivery and low-level observation, separate from Minecraft. A hidden fixture launch was inconclusive because its window could not receive focus; it is not counted as a pass |
| Velocity | 10 baseline trials and 10 each at zero, 50%, 100%, and X/Z 92.55% / Y 63.12%. All 100 native motion callbacks matched requested post-receive component values. Maximum fractional formula error was below 4e-9. Zero native faults and no skipped trials | Combine Modifiers ON, grounded controlled incoming knockback. Client mean peak horizontal movement: baseline 1.224 blocks, zero 0.00075, half 0.683, full 1.291, fractional 1.039. Server corrections affect trajectories; displacement is not assumed linear or equivalent to server motion |
| Aim Assist | 15 synchronized phases: baseline and disabled view stayed fixed; horizontal-only preserved pitch; vertical-only preserved yaw; both axes converged within the configured dead zone. Stationary/idle-combat/player-only gates prevented steering. Wall avoidance blocked steering; disabling it allowed correction. Avoid On Target stopped earlier. Blatant converged to the target anchor | One owned villager; real-player acquisition and multiple-player filtering are separate coverage. Zero native faults |
| Reach | At the extended fixture distance, 3.0-block and disabled trials had no server hits. 5.2-block Reach produced 78 accepted hits and 9 damage points in the stable comparison window. Idle combat and stationary-only gates produced none | Ray/AABB distance, authorized local-world server acceptance. Counts exclude phase-transition margins |
| Hitbox | Off-axis baseline and width 0.4 produced no server hits; width 2.22 produced 76 accepted hits and 9 damage points. Combined Reach/Hitbox produced 77 accepted hits and 9 damage. A solid occluder blocked entity hits | Absolute virtual targeting width; physical collision geometry is retained |
| Timer | Stabilized local actor tick rates: baseline 20.03, 0.8x 15.67, 1.1x 22.13, 1.3x 26.03, disabled 20.05 TPS | Independent server remained approximately 20 TPS. No claim of server-clock control |
| Zoom | Render FOV changed from 2.2688 to 0.79409 radians, factor 0.35; visible zoom and restoration after disable were inspected | Speed 50 transition; exact tagged render camera, not an arithmetic-only preview |
| Auto 360 | Observed unwrapped native yaw travel 359.99997 degrees and one completed turn after an accepted attack | One right-weighted turn at 1 turn/second, with Aim suspended during the spin |
| Mining | Native held mining broke one owned dirt block in Survival, confirmed by the independent server block event | Auto/Legit delay and release policy additionally covered by planner tests |
| Main application, combined session | F8 armed Aim, Reach, Hitbox, Triggerbot Clicker at 18 CPS, Timer and Zoom. Over 27.212 seconds, 488 scheduled requests reached 488 native actions and 488 independent server hit events, with 55 damage points. Local TPS was approximately 26 while the server stayed near 20. F9 stopped output and restored Timer/FOV | Actual AsterClient controller, separate from the developer probe; zero native faults. The configured Velocity path was idle in this particular combined run |
| Main application, movement and interruption | Auto Walk moved the local player over 100 blocks; F9 stopped horizontal motion. Restoring the Aster window stopped a newly armed run on focus loss. Opening inventory stopped a combined run; closing it left action counts unchanged until another explicit arm | No physical-key assistance needed. These results do not establish the full physical W + mouse S-Tap path |
| Adapter performance | After optimizing native memory checks and immutable block-name lookup, mean callback time was 3.17 ms disabled, 4.24 ms with Aim and 4.33 ms with Clicker in selected stable windows of a three-minute run; previous disabled mean was 17.50 ms | Measurements on this host and fixture, not an FPS guarantee. An 18 CPS probe window separately recorded 354 requests, native attacks and server hits over 20 seconds; the probe clock's later drift correction is covered separately by the scheduler tests |

## Coverage limits

All listed modules have executable handlers; the following are gaps in validation, not claimed passing scenarios:

- The final physical W/button Clicker and S-Tap walkthrough was omitted at the user's request. Timing, cancellation and key ownership have automated coverage, but a complete physical-input Minecraft S-Tap run and a dedicated physical Sprint run were not observed.
- Native right-click placement previously produced eight independent server block events. The final change to Bedrock's start-build entry point was compiled and reviewed, but placement was not repeated after that change. No claim of 30 accepted block placements per second is made.
- Velocity's controlled live series used Combine Modifiers ON. Delta composition and No Backwards have mathematical tests; all combinations, angles and combat gates were not repeated in-game.
- Aim tests used one owned villager. Multi-player acquisition, every filtering combination, rapid dimension/reconnect transitions and prolonged play on other machines remain unmeasured. Native world identity and target validity are checked before applying effects.
- The final protocol-8 read-only probe loaded the exact-build adapter, observed the world with input released, and exited without enabling modules. It validates loading/layout/cleanup, not another gameplay pass of every module.

This report does not describe proprietary parity or exhaustive gameplay testing as complete.

## Portable package verification

The final Release targets built successfully. CTest reran the 34 core/platform cases with zero failures. The timestamped portable package installed 23 files plus its generated hash manifest; every installed hash, the ZIP hash, both x64 PE headers and archive contents were checked. It includes both the executable and native DLL, all notices, documentation and optional service source. Research, game binaries/worlds, private data and debug symbols are absent.

The ZIP was extracted under a fresh ignored test directory and launched from there with an isolated profile for a five-second hidden smoke run. It verified the actual Minecraft executable hash, connected the packaged native bridge without arming, saved the isolated profile, shut down normally and returned exit code 0. The existing Minecraft process remained running. This is an installation/startup/cleanup test; visible gameplay and rendering were inspected in the earlier main-client runs above.

## Local evidence index

Development reports are intentionally ignored by Git. Main results came from `velocity-50-world.json` and its summary/server reports, `aim-suite-world.json`, `targeting-suite-world.json`, `timer-factors-world.json`, `zoom-world.json`, `spin-world.json`, `mining-world.json`, `performance-active-world.json`, and the `ui/combined-*.json` and `ui/walk-*.json` reports under `test-results/`. Native protocol revisions and setting snapshots are retained in those reports; they should not be confused with the later read-only protocol-8 probe. The final test log is `build/Testing/Temporary/LastTest.log`.

## Reproduction

Build the client and `AsterBridgeTool`, then use the optional pack under `tests/bedrock_qa` only in a disposable world. `tools/qa_plan.py` generates synchronized Aim/targeting scenarios and prints their matching script command. Start the CLI before the common future epoch, return to captured gameplay, and retain both native JSON and `[AsterQA]` events. Reports must include their setting snapshot, timestamp, exact game build/hash, gates and actual observation counts.

## Limits

Only the named executable is supported. External servers can reject extended targeting, movement or timing and can correct velocity. Voxel line of sight is conservative for partial/non-solid blocks and treats glass as an occluder. Antibot is explicitly a structurally valid player-only filter, not universal NPC detection. Exact proprietary Nitro algorithms, other hardware/DPI combinations, other game versions, public code signing and hosted commercial services are not established by this report.
