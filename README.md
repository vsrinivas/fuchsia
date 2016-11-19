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

**Setup, build and run** instructions for Fuchsia are
[here](https://fuchsia.googlesource.com/manifest/+/master/README.md). Once setup
and build is done, you can start the TQ Framework flow thus:

```sh
@ bootstrap device_runner --user-shell=dummy_user_shell
```
