# Zircon

Zircon is the core platform that powers the Fuchsia OS. Zircon is composed of a
[microkernel](/zircon/kernel) as well as a small set of userspace services,
drivers, and libraries in [/zircon/system](/zircon/system) necessary for the
system to boot, talk to hardware, load userspace processes and run them,
etc. Fuchsia builds a much larger OS on top of this foundation.

See the documentation at: https://fuchsia.dev/fuchsia-src/zircon
