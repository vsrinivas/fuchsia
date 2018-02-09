# launchpad

[launchpad][launchpad] is a Zircon library that provides the
functionality to create and start new processes (including loading ELF
binaries, passing initial RPC messages needed by runtime init, etc).
It is a low-level library and over time it is expected that few pieces
of code will make direct use of it.

Launchpad is designed to give complete control over the creation of a
new Process. This includes:
- The executable code that will be loaded into the process.
- The initial Handle table of the process.
- The initial set of [file descriptors](libc.md#fds) in the process.
- The process's view into filesystems.
- The unix environment (as in getenv and setenv) of the process.

There is extensive documentation about launchpad in [its primary
header file][launchpad-header].

[launchpad]: https://fuchsia.googlesource.com/zircon/+/master/system/ulib/launchpad "launchpad"
[launchpad-header]: https://fuchsia.googlesource.com/zircon/+/master/system/ulib/launchpad/include/launchpad/launchpad.h "launchpad header"
