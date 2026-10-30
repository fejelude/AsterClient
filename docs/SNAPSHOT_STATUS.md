# Source upload status

This repository is a development snapshot, not a finished Nitro replacement.

The Windows control application builds. Earlier core/platform testing passed 23 cases, and the included own-license service passed its integration suite. The most recent experimental bridge changes have not completed in-world validation.

`src/integration/bridge_dll.cpp` and `tools/bridge_probe.cpp` contain an experimental, exact-build observer and a controlled timer test. These are separate from the main control application. The observer validated actor coordinates and bounding-box data in a newly created disposable flat world on Minecraft package build 1.26.6005.0. The timer effect test was not completed. The probe was stopped and its timer restored when the user stopped Computer Use.

The probe currently requires a local research hash record under the ignored `docs/spec-review/research` directory. Private research files, reference screenshots, Minecraft binaries, generated credentials, service databases, local test reports, build products and analysis tools are deliberately excluded from this repository.

Some documentation linked from README is still planned and absent. Earlier documents describe the native application milestone; the experimental bridge work is newer. Gameplay modules must not be described as finished on the basis of this source upload.

Dependencies are pinned in `dependencies.lock.json`; permissive third-party license notices accompany the source. No proprietary Nitro implementation, paid license protocol or private signing material is included.
