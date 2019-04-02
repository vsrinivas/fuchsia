# Fuchsia Rust Crates

* [fdio/](/garnet/public/rust/fdio/)

    Wrapper over zircon-fdio library

* [fuchsia-app/](/garnet/public/rust/fuchsia-app/)

    Tools for working with Fuchsia Services and Applications

* [fuchsia-archive/](/garnet/public/rust/fuchsia-archive/)

    Work with Fuchsia Archives (FARs)

* [fuchsia-async/](/garnet/public/rust/fuchsia-async/)

    Fuchsia-specific Futures executor and asynchronous primitives (Channel, Socket, Fifo, etc.)

* [fuchsia-ddk/](/garnet/public/rust/fuchsia-ddk/)

    Rust Driver Development Kit for Fuchsia (experimental - DO NOT USE)

* [fuchsia-device/](/garnet/public/rust/fuchsia-device/)

    Rust bindings to common Fuchsia device libraries

* [fuchsia-framebuffer/](/garnet/public/rust/fuchsia-framebuffer/)

    Configure, create and use FrameBuffers in Fuchsia

* [fuchsia-merkle/](/garnet/public/rust/fuchsia-merkle/)

    Protect and verify data blobs using [Merkle Trees](../../../the-book/merkleroot.md)

* [fuchsia-scenic/](/garnet/public/rust/fuchsia-scenic/)

    Rust interface to Scenic, the Fuchsia compositor

* [fuchsia-syslog-listener/](/garnet/public/rust/fuchsia-syslog-listener/)

    Implement fuchsia syslog listeners in Rust

* [fuchsia-syslog/](/garnet/public/rust/fuchsia-syslog/)

    Rust interface to the fuchsia syslog

* [fuchsia-system-alloc/](/garnet/public/rust/fuchsia-system-alloc/)

    A crate that sets the Rust allocator to the system allocator. This is automatically included for projects that use fuchsia-async, and all Fuchsia binaries should ensure that they take a transitive dependency on this crate (and “use” it, as merely setting it as a dependency in GN is not sufficient to ensure that it is linked in).

* [fuchsia-trace/](/garnet/public/rust/fuchsia-trace/)

    A safe Rust interface to Fuchsia's tracing interface

* [fuchsia-vfs/](/garnet/public/rust/fuchsia-vfs/)

    Bindings and protocol for serving filesystems on the Fuchsia platform

* [fuchsia-vfs/fuchsia-vfs-watcher/](/garnet/public/rust/fuchsia-vfs/fuchsia-vfs-watcher/)

    Bindings for watching a directory for changes

* [fuchsia-zircon/](/garnet/public/rust/fuchsia-zircon/)

    Rust language bindings for Zircon kernel syscalls

* [mapped-vmo/](/garnet/public/rust/mapped-vmo/)

    A convenience crate for Zircon VMO objects mapped into memory

* [mundane/](/garnet/public/rust/mundane/)

    A Rust crypto library backed by BoringSSL

* [shared-buffer/](/garnet/public/rust/shared-buffer/)

    Utilities for safely operating on memory shared between untrusting processes

* [zerocopy/](/garnet/public/rust/zerocopy/)

    Work with values contained in raw Byte strings without copying
