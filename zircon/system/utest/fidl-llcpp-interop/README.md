## Generated LLCPP code for unit tests

These files (`fidl_llcpp_basic.cpp`, `fidl_llcpp_basic.h`, etc) are checked in for the time being,
due to the Golang-based `fidlgen` not accessible from Zircon yet.
To generate these files, first build Zircon and Garnet using `fx`, then run this command:

```bash
fx exec zircon/system/utest/fidl-llcpp-interop/gen_llcpp.sh
```

It can be run from any location.

Whenever the llcpp codegen in Garnet is updated, these files should be re-generated and checked in.

As soon as the merger between the Garnet repo and Zircon happens, we should modify the build system
to automatically generate llcpp code, at which point we can remove this directory.
TODO(FIDL-427): replace manual code generation with automated solution.
