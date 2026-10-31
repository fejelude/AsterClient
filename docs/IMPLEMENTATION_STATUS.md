# Implementation status

This original application follows the supplied 27-page clean-room specification and 23-page ASTER feature guide. Reference screenshots and proprietary artwork are not distributed as application assets.

The main application now owns the exact-build native adapter. Typed configuration reaches real Reach, Hitbox, Aim, Velocity, Timer, Clicker/mining, Zoom and Auto 360 handlers. Host input planning implements independent CPS, priming, jitter, inversion, Triggerbot, Walk, Sprint and S-Tap. Saved state cannot arm itself: F8 starts a focused-world session; interruption requires a new explicit arm.

The host/bridge protocol validates version, session, heartbeat and pinned world generation. Effects stop on focus/capture/world loss, F9, shutdown or invalid layout. Timer/FOV writes carry ownership and restore their baseline. Native callbacks borrow POD data; foreign strings and ownership-bearing game objects are never constructed or destroyed by the adapter.

The original UI, appearance, optional hotkeys, atomic profiles, recovery, diagnostics, Credential Manager, independent licensing service and signed-update verification remain implemented. Commercial service hosting, purchase destinations and trusted code signing require owner-provided infrastructure. The developer build works locally without a purchase key.

See [runtime contract](RUNTIME_CONTRACT.md), [compatibility](COMPATIBILITY.md), and [validation](VALIDATION.md). Real effects were measured through both the developer probe and the main application. Validation lists the exercised scenarios and the remaining manual coverage, including the complete physical W/button S-Tap path. Exact proprietary algorithms, universal multiplayer authority and future binaries are not established.
