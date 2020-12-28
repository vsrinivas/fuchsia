The Fuchsia Volume Manager exposes a set of virtual partitions that can grow or shrink. See
[filesystems concepts](/docs/concepts/filesystems/filesystems.md) for more detail.

The toplevel `fvm` directory contains the shared code for dealing with the FVM format.

  * The `driver` runs on Fuchsia and exposes the virtual partitions to the rest of the system.

  * The `host` directory contains code that runs on the development machine to generate'
    FVM images.
