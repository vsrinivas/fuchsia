# Fuchsia VFS

Fuchsia VFS implements bindings and protocol for serving filesystems on the
Fuchsia platform. It implements both the remoteio protocol and an abstraction
around VFS serving.

## Rust VFS

Rust VFS and rust pseudo directories have rustdoc style documentation which can
be generated with command like

```
fx gen-cargo //src/lib/storage/vfs/rust:vfs
cargo +nightly doc --manifest-path src/lib/storage/vfs/rust/Cargo.toml --target x86_64-fuchsia --no-deps
```

The generated docs can be found in
`src/lib/storage/vfs/rust/target/x86_64-fuchsia/doc`.

See also: `cargo doc --help`.
