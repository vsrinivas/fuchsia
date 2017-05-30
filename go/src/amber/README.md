# Amber: An update system for Fuchsia

Amber is an update system with the ambition of updating all components running
on a Fuchsia system whether that component is a kernel, a bootloader, a system
service, an application, etc.

Many things are uncertain and/or undecided and this uncertainty is increased by
the fluidity of other parts of the system with which Amber must interoperate.
We do know that we are exploring using [The Update Framework (TUF)](http://theupdateframework.com/)
as a basis for secure distribution. We use the Go implementation of TUF from
https://github.com/flynn/go-tuf which we mirror at http://fuchsia.googlesource.com/third_party/go-tuf.
Many thanks to the Flynn team for such an excellent implementation in Go.
