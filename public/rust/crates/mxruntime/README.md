Rust bindings for Magenta runtime services
==========================================

This repository contains bindings for Magenta runtime services other than those
directly provided by kernel syscalls. At the moment, that's primarily access to
startup handles, but this also the correct place for launchpad bindings.

The low level bindings are in a subcrate mxruntime-sys, while the main crate
contains a type-safe wrapper in terms of native Rust types.
