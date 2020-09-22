GN support for Zircon
=====================

TODO(fxbug.dev/3156): This will go away when the build is fully unified.

Code in //build/config/fuchsia/zircon.gni populates the
[`//zircon/public`](../../zircon/public) directory tree with links to
[`template.gn`](template.gn).  This code generates targets for its
directory based on instructions written as JSON by the Zircon `gn gen`
step.  That code uses the `.gni` files here.

### Process

The main build file [`//BUILD.gn`](../../BUILD.gn) imports
[`//build/config/fuchsia/zircon.gni`](../config/fuchsia/zircon.gni).
That makes that GN code run before all other evaluation in `gn gen`.
This ensures that non-Zircon build files can safely depend on the
generated targets.

This early GN code has the side effect of populating the `//zircon/public`
subdirectories with `BUILD.gn` files that are links to
[`template.gn`](template.gn).  This happens on every `gn gen` run, but all
it does is maintain those directories of `BUILD.gn` links.  So multiple
separate Fuchsia builds can refer to multiple separate Zircon builds
without mutual interference, as long as they are referring to the same
Zircon source tree so there's the same set of `//zircon/public`
subdirectories to populate.

Each file uses its portion of the instructions written as JSON by the
Zircon `gn gen` step to define all the appropriate targets for that
directory.  Hence, the actual operation of each
`//zircon/public/.../BUILD.gn` file is controlled by the particular Zircon
build attached to the Fuchsia build, but the files themselves are fixed.
