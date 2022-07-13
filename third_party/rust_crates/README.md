# Vendored Fuchsia Rust packages (crates.io mirroring)

This directory contains vendored copies of third party code used in Fuchsia.

Further documentation is available here:

//docs/development/languages/rust/external_crates.md

## macOS

You will need a modern OpenSSL and to export either `PKG_CONFIG_PATH` or `CFLAGS` and `LDFLAGS` in order for cargo to find it, before running `fx update-rustc-third-party`.

You can install using `brew install openssl` and observe `brew info openssl` for the above exports. It is recommended you only export these as-needed, not permanently, to avoid causing incompatibilities with other software.
