# Overview

This directory contains testing utilities for working with a CFv2-compatible
[Isolated DevMgr component](../README.md).

## Usage
There are two main ways to use the CFv2 IsolatedDevMgr:
1. Define an [`isolated_devmgr_v2_component`](isolated_devmgr.gni) component locally, which exposes
   a `/dev` directory. With this GN template, the client is responsible for routing the `/dev` dir
   into their test component's namespace as necessary. This would generally be via CML, but could
   also be done at runtime in Rust via the fuchsia_component_test::RealmBuilder.
2. Define an [`isolated_devmgr_unittest_v2_component`](isolated_devmgr.gni) component to be a child
   component of the test component, ensuring it is packaged alongside the test component. Then, one
   uses either the C++ [OneTimeSetup](bind_devfs_to_namespace.h) or Rust
   [`launch_isolated_driver_manager`](rust/src/lib.rs) utility to bind the isolated driver manager's
   `/dev` directory into the test component's namespace dynamically.

## Internals

We'll examine the GN targets in this directory to get a better sense of how they fit together:

### Core library elements:

- ":driver-manager-test" GN target: Contains the production Driver Manager/Driver Host binaries,
    i.e. the underlying driver management logic. This component runs the binaries with capabilities
    from its parent - so this makes the device  manager isolatable, but does not actually provide the
    capabilities which make the device manager "isolated".
- ":support" GN target: The device manager binaries require access to the
    [kernel/RootJob](https://fuchsia.dev/reference/fidl/fuchsia.kernel?hl=en#RootJob) and
    [boot/Arguments](https://fuchsia.dev/reference/fidl/fuchsia.boot?hl=en#Arguments) capabilities, so this helper binary
    provides a fake implementation of these services for the IsolatedDevMgr.

### Client utilities:

- isolated_devmgr.gni: This GNI file defines two templates, both of which are mentioned in
    [Usage](#Usage). The first, `isolated_devmgr_v2_component`, takes only a package name. It
    defines an IsolatedDevMgr component that exposes a directory capability named `dev` in that
    package. The second, `isolated_devmgr_unittest_v2_component`, takes a test executable and
    package name as input and generates a unit test component with a child IsolatedDevMgr component.
    Tests that use this template generally use one of the following client libraries.
- ":client" GN target: This source_set provides C++ utility functions for the IsolatedDevMgr. The
    bind_devfs_to_namespace header can be used to set up a test component's `/dev` directory with
    the isolated devfs exposed by a child IsolatedDevMgr.
- "rust:isolated-driver-manager" GN target: Similar to the C++ client target, this provides Rust
    library functions for working with an existing child IsolatedDevMgr component, including a
    helper to bind the child IsolatedDevMgr's `/dev` directory to the test component.