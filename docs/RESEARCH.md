# Source and evidence register

Reviewed October 30, 2026. The supplied 27-page PDF was extracted in full and all 35 atlas screenshots on pages 18-26 were visually inspected. Local inspection material is excluded from release/source ZIPs. The PDF is the main behavioral guide, not authorization to copy proprietary assets or hidden protocols.

| Source | Finding / decision |
|---|---|
| [Dear ImGui official getting started](https://github.com/ocornut/imgui/wiki/Getting-Started) | Official Win32/DirectX 11 integration; unmodified example built and visually executed first. |
| [Dear ImGui pinned source](https://github.com/ocornut/imgui/tree/f5befd2d29e66809cd1110a152e375a7f1981f06) | 1.91.9b, MIT. Only this UI library and its official backends are included. |
| [nlohmann/json pinned source](https://github.com/nlohmann/json/tree/55f93686c01528224f448c19128836e7df245f72) | 3.12.0, MIT. Strict typed loading plus explicit migration/default recovery. |
| [Nitro site](https://nitr0.club/) and [README fork](https://github.com/v-irels/Nitro) | Product/reference descriptions; no complete independently licensed implementation or protocol is supplied. No binaries, endpoints, artwork or secrets copied. |
| [MCBE DX12 ModBase](https://github.com/b01dev/MCBE_DX12_ModBase) | MIT architecture reference; explicitly version-scoped to Bedrock 26.52. It is not evidence for 1.26.6005.0 compatibility. No code imported. |
| [Archived Horion](https://github.com/horionclient/Horion) | Archived December 2021; noncommercial license in the PDF. Historical architecture only, no code imported. |
| [Limerence client](https://github.com/LimerenceMC/client) | GPL example reference. No code imported or linked. |
| [Latite releases](https://github.com/LatiteClient/Latite/releases) | Additional current-version investigation: visible stable release supports 26.44; no verified 26.60 adapter established from it. No code imported. |
| [Minecraft official 26.60 changelog](https://www.minecraft.net/en-us/article/minecraft--bedrock-edition-26-60-changelog) | The public release name and Store package build are different strings. The app reads the package build instead of assuming equivalence. |
| [Microsoft Bedrock server API](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server/minecraft-server) | Documented scripting context is not a universal substitute for game-process movement/combat hooks. No add-on is installed or claimed to implement those hooks. |
| [GetWindowThreadProcessId](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid) | Window identity is checked with process/package/architecture metadata and foreground gating. |
| [Credential storage](https://learn.microsoft.com/en-us/windows/win32/SecBP/handling-passwords) | Refresh sessions use CredWrite/CredRead, local-machine persistence under the logged-in user, not settings JSON. |
| [RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey) | Global Hide and F9 stop use MOD_NOREPEAT and report collisions. Module toggles remain game-focused. |
| [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput) | Success is an OS delivery count, not game acknowledgment. UIPI remains in effect; the app does not elevate or bypass it. |
| [Microsoft signing guidance](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control) and [SignTool verification](https://learn.microsoft.com/en-us/windows/win32/seccrypto/using-signtool-to-verify-a-file-signature) | A trusted publisher certificate is an external release prerequisite. The development EXE is unsigned and delivered with hashes. |
| [Defender developer FAQ](https://learn.microsoft.com/en-us/defender-xdr/developer-faq) | No exclusions or security suppression. Any actual detection requires developer review/submission, not evasion. |

## Defined independent behavior

The code does not silently infer proprietary algorithms. Independent CPS jitter is uniform interval scaling in `[0.88,1.12]`; mean requested interval remains `1/CPS`. Left priming mirrors the demonstrated right-priming rule and is labeled independent. Invert Clicks swaps logical output channels. Local numeric ranges not established by the PDF are conservative configuration bounds, documented by the setting definitions in `src/core/model.cpp`. None establishes an engine effect.

The two unclear opacity controls have explicit independent roles: control-panel opacity and background texture opacity. Image Speed drives original contour animation; imported bitmaps remain still. Anti OBS is explicitly unsupported. Discord/Vouch controls are explicitly unavailable because no destination exists for this independent app.

## Remaining evidence needed

- A verified, appropriately licensed engine integration for the exact installed game build, or a complete independently researched and tested port.
- Measured target selection, reach/server authority, velocity/knockback timing, timer targets, camera restoration and hitbox semantics.
- A demonstrated definition or agreed independent design for Tuner, block-breaking modes, Auto S-Tap, Auto 360 units, Zoom factor and undisclosed filtering rules.
- Reproducible before/after observations in disposable private worlds, including failure/cleanup cases and performance at multiple frame rates.
- Owner-operated production service/URLs and a trusted publisher certificate for commercial release operations.
