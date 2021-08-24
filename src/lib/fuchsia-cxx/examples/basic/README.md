# Basic CXX example

This example is a slightly modified version of the demo from https://github.com/dtolnay/cxx, adapted to demonstrate usage of cxx within the Fuchsia tree.

It also includes both a Rust and C++ implementation of the toy main() that exercises the FFI to demonstrate how to use the generated FFI from either direction.

For general information about using cxx, see https://cxx.rs and https://docs.rs/cxx.

## Files

- `BUILD.gn` - Definitions of GN build targets showing how to use Fuchsia's `rust_cxx_ffi_source_set` template and how to use such a target in downstream Rust and C++ targets.
- `src/lib.rs` - Contains the `#[cxx::bridge]` module which defines the type and function signatures for both the Rust and C++ sides of the FFI boundary, as well as implementations of types and functions on the Rust side.
- `src/blobstore.cc` - Implementations of types and functions on the C++ side.
- `blobstore.h` - Declarations of types and functions on the C++ side.
- `main.rs` - Source for a Rust binary that exercises the `BlobstoreClient` FFI defined in the above files.
- `main.cc` - Source for a C++ binary that also exercises the `BlobstoreClient` FFI, and has equivalent behavior to `main.rs`.
