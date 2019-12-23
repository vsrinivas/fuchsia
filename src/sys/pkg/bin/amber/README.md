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

## The Grand Unified Binary

The `amber` package contains more than just the `amberd` binary. It actually
contains all of the Go the binaries required for all of the software delivery
subsystem, including `pkgsvr`, `amberd`, and `system_updater`, as well as
sharing binary contents with the `amberctl` program in the `amber`
package. This is a space saving optimization, as it reduces the total binary
footprint of the software delivery subsystem by more than 50% due to the
overhead of Go binaries and their lack of shared library support. Despite
sharing a binary image, the components are still executed as independent
components and do not share address space.
