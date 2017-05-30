# Amber: An update system for Fuchsia

Amber is an update system with the ambition of updating all components running
on a Fuchsia system whether that component is a kernel, a bootloader, a system
service, an application, etc.

Many things are uncertain and/or undecided and this uncertainty is increased by
the fluidity of other parts of the system with which Amber must interoperate.
We do know that we are exploring using [The Update Framework (TUF)](http://theupdateframework.com/)
as a basis for distribution and thus you will find many similarities between
this repository and the Go implementation of TUF from
https://github.com/flynn/go-tuf. Many thanks to the Flynn team, we hope to
upstream any relevant changes!

Note: The go-tuf README can be found [here](README-tuf.md).
