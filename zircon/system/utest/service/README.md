# FIDL Service tests

This directory contains unit tests for FIDL Services, specifically the Low Level C++
(LLCPP) client side and server side APIs.

See `//zircon/system/ulib/fidl` and `//zircon/system/ulib/service`.

## How to build

When making changes to `test.fidl`, you must run the `gen_llcpp.sh` script to regenerate
the LLCPP bindings. LLCPP bindings are not generated in the Zircon build system.

`fx exec zircon/system/utest/service/gen_llcpp.sh`

The generated FIDL C++ files are placed into `//zircon/system/utest/service/generated/`.
Check these in to version control.

## How to run

```
fx set ... --with //zircon/system/utest/service:tests
fx test -od service-llcpp-unittest-package
```
