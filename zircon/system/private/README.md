## Zircon Private Headers

This directory is in the include path for all kernel and system
modules in Zircon, but unlike system/public, it is *not* exported
into the include directory of the generated sysroot.

Most headers should live with their respective libraries.  These
headers are ones that need to be global because they define protocols
or utilities shared between user and kernel or between user modules,
that *are not* publicly visible outside of Zircon.

