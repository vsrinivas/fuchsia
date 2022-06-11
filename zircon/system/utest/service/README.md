# FIDL Service tests

This directory contains unit tests for FIDL Services, specifically the Low Level C++
(LLCPP) client side and server side APIs.

See `//sdk/lib/fidl/llcpp` and `//zircon/system/ulib/service`.

## How to run

```
fx set ... --with //zircon/system/utest/service:tests
fx test -od service-llcpp-unittest-package
```
