# Source snapshot

The native adapter is integrated with AsterClient. AsterBridgeTool provides separate developer scenarios; original QA pack and scenario generator source are included. The adapter no longer requires a private research hash file: its exact supported hash is compiled into source.

Read [validation](VALIDATION.md) for measurements and coverage limits. Source includes neither Minecraft nor Nitro binaries. PDFs/images, disassembly, game dumps, user worlds/options, raw reports, credentials, databases and build products are excluded. Dependency source/hashes and permissive notices remain reproducible from `dependencies.lock.json`. The portable package includes the client, bridge, notices, documentation and optional owner-service source; it installs no test pack automatically.
