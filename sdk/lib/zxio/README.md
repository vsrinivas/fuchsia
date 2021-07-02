# zxio

The zxio client library provides an object oriented abstraction on top of basic
I/O primitives such as files, pipes, directories, and sockets. Internally, this
library uses the FIDL fuchsia.io* family of protocols and Zircon syscalls.

## Linkage

The zxio library is stateless and can be used as a static library. It internally
uses the C++ standard library.

## zxio_standalone

libzxio_standalone.so is provided as a standalone shared library version of the zxio.