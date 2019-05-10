# vfs_cpp

This library is included in the SDK to support the `sys_cpp` library.
Currently, the library cannot be used to implement a production-quality
file system. Instead, the libraries scope is limited to the psuedo file
systems that components expose in their outgoing namespace for service
discovery and introspection.

Please do not use headers from the `internal` directory or symbols in the
`internal` namespace. Currently, this library lacks a coherent concurrency
model. Many of the core classes are contained in an `internal` directory and
namespace to indiciate that these classes are likely to change dramatically as
we develop the concurrency model.
