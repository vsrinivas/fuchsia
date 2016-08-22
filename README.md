Modular
=======

Modular is an experimental application platform.

It provides a post-API programming model. Rather than basing all
service calls on pre-negotiated contracts between known modules, the
system enables modules to
[call by meaning](http://www.vpri.org/pdf/tr2014003_callbymeaning.pdf),
specifying goals and semantic data types along side traditional typed
function inputs and outputs, and letting the system work out how those
semantic goals are satisfied and what services are employed.

## Writing for the Modular platform

Modular runs in any [Mojo](https://github.com/domokit/mojo) shell. Modules may
be written in any language supported by the shell, and a library is included
for more rapid development using [Flutter](http://flutter.io/) and Dart.

**Setup, build and run** instructions are in
[HACKING.md](https://fuchsia.googlesource.com/modular/+/master/HACKING.md).
