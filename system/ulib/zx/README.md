# The C++ zx library

The intention of this library is to provide an idiomatic C++ interface
to using Zircon handles and syscalls. This library provides type
safety and move semantics on top of the C calls.

This library does not do more than that. In particular, thread and
process creation involve a lot more than simply creating the
underlying kernel structures. For thread creation you likely want to
use the libc (or libc++ etc.) calls, and for process creation the
launchpad APIs.

This library is usable both within the Zircon repo as well as the
general Fuchsia gn build.
