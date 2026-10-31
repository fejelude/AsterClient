# Private-world observer

This optional behavior pack is an independent server-side test fixture. AsterClient does not load, require or communicate with it during normal use. Install it manually only in a new disposable flat test world with cheats enabled and select an empty hotbar slot. Fixture commands use coordinates around `(1000, -60, 1000)` and must not be used in an existing build.

The manifest requests the stable `@minecraft/server` 2.0.0 API. Enable content logging to collect lines beginning `[AsterQA]`. The pack records accepted entity hits, actual damage, block events, server ticks, positions and rotations. Those observations must be compared with native bridge reports; a native dispatch alone is not server acceptance.

Useful commands:

```text
/scriptevent aster:setup {}
/scriptevent aster:heal {}
/scriptevent aster:trial {"name":"baseline","ticks":400}
/scriptevent aster:knockSeries {"name":"knockback","trials":10,"period":60,"ticks":720}
/scriptevent aster:blocks {"name":"mining","placing":false,"ticks":500}
/scriptevent aster:blocks {"name":"placing","placing":true,"ticks":500}
/scriptevent aster:cleanup {}
```

For synchronized tests, `aster:sequence` accepts `suite: "aim"` (15 four-second phases) or `suite: "targeting"` (12 five-second phases), a Unix-millisecond `startAt`, and a report `name`. Match that epoch to an `AsterBridgeTool --scenario` plan. `knockSeries` also accepts an absolute epoch with `groupSize`, `phaseMs` and `periodMs`; late trials are logged rather than burst together. Start the epoch far enough in the future to finish entering the command and return to gameplay.

The target is an owned villager named `AsterQA:Target`. Reset heals and teleports only that fixture. The block fixture refuses occupied space and a nonempty selected slot. Its item has a unique name and cleanup removes only that item and expected fixture block types. The targeting suite temporarily owns a small stone occluder and removes it at its final phase or `cleanup`.

Finish each run with cleanup, restore the test world's mode/view as needed, and disable content logging if it was enabled only for QA. Preserve raw reports locally; do not publish user worlds, game dumps, account identifiers or full content logs.
