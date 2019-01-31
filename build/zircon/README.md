GN support for Zircon
=====================

This folder contains utilities that help Zircon artifacts better integrate into
the GN build without having to manually maintain GN build files in sync with the
existing `rules.mk` build files specific to Zircon.
It also makes it so that targets in upper layers don't need to know about the
structure of Zircon's output directory in order to use Zircon build artifacts,
as knowledge of that structure is confined within the generated GN build files.

### Process

The main build file [`//build/gn/BUILD.gn`](../gn/BUILD.gn) calls the
[`//build/zircon/create_gn_rules.py`](create_gn_rules.py) script to produce
`BUILD.gn` files for Zircon.

That script uses a special, fast build of Zircon (`make packages`) to generate
manifests for artifacts that are meant to be consumed by upper layers of the
Fuchsia stack. These manifest are then turned into GN build files under
[`//zircon/public`][zircon-public] whose targets other parts of the codebase
can depend on.

This script is run early in the build process, before any other build file gets
evaluated. This ensure that non-Zircon build files can safely depend on the
generated targets.

In order to keep the generated build files up-to-date, that process is repeated
every time a file changes under `//zircon`.

Note that the generated GN files should only depend on the Zircon source code.
Invoking the Fuchsia build with different parameters on the same codebase should
produce identical files.


[zircon-public]: https://fuchsia.googlesource.com/zircon/+/master/public/
