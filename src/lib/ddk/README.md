This library is not long for this world.  Please do not add new things to it.
Bug: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=32349

The plan is to split this library up into several parts:
1) Most of the headers in include/ddk/ should be moved to ulib/driver (which
   itself will be relocated to dev/lib/driver).
2) io-buffer, mmio-buffer, and phys-iter should be moved into dev/lib/io-buffer
   as a static library
3) The headers in include/hw should be moved to per-device-type libraries
