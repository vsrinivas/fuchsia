# Minfs micro benchmark

Minfs micro-benchmarks  are targetted to keep check on a few aspects of the
filesystem performance. The suite is designed to catch regressions and to help
one understand the impact of their changes at micro level.

The basic principle is

* perform a micro task with one filesystem operation.
* ask the underlying storage layer for changes in stats.
* compare those stats against an expected result.

For example, if we know device properties, mkfs and mount options, then we
can tell with certainty what blocks mount should read and write to.
Similarly, if there were no operations performed after mount, we can tell what
IOs an unmount should perform. Using such building blocks we should be able to
understand and keep track of IO stats for more complex FS operations like write,
create and delete, etc.

Since we want our accounting to check those of filesystem, we intend to maintain
our own accounting routines for FS operations. These routines, like AddMountCost,
will rely on the state (like clean/dirty) of the filesystem. These routines
compute their number from first principles and their definitions can be found in
*-costs.cc files.
