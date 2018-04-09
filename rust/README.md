# Rust Build Targets

This folder is currently in an intermediate state, and contains two
separate templates for building Rust. The `rust_xxx` rules are being
replaced by the `rustc_xxx` rules, which build using rustc directly
rather than depending on Cargo to build the Cargo Workspace inside of
Garnet.

The `rustc_xxx` rules use the following scripts:

- `build_rustc_target.py`
- `compile_3p_crates.py`
- `write_cargo_toml.py`

All other scripts are legacy dependencies of the `rust_xxx` scripts.
