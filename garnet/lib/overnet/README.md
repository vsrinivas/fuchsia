Overlay Network
===============

This library contains the core protocol and logic for the Fuchsia overlay network. This is a mesh network that sits atop other network protocol stacks and allows message routing.

Build and Runtime Environment Considerations
--------------------------------------------

The library is intended to be used both within Fuchsia, and outside Fuchsia on existing operating systems and within other projects.

This library should depend on only the C++ runtime library at runtime, and additionally google test and google mock for test code, and no more.

This library is expected to be _wrapped_ by other binaries and libraries to produce a full system. This expectation allows us to set down the rule that **no OS interactions are to occur within this library**. Instead, interfaces should be phrased such that relevant OS interactions can be injected by more specific code.

To verify this library works outside of Fuchsia:
ninja -C out/x64 host_x64/overnet_unittests && out/x64/host_x64/overnet_unittests
