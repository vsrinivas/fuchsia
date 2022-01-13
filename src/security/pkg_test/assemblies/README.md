# System Assemblies for Security Package Delivery Tests

This directory contains very small system assemblies for use in security package
delivery tests. These assemblies are used to define the running system and/or
system update in the hermetic package delivery system.

## Assembly Listing

1.  `hello_world_v0`: A minimal system that contains a "Hello, World!" program.
    This assembly is usually used as an initial running system. The system
    update version is intended to encode "Version 1.0.0, released 1 January
    1970".
1.  `hello_world_v1`: A minimal system that contains a "Hello, World!" program
    that is different from the one assembled in `hello_world_v0`. This assembly
    is usually used as an update to the `hello_world_v1` assembly. The system
    update version is intended to encode "Version 1.1.0, released 1 January
    1970".
