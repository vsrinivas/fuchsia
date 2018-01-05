#!/bin/bash
FUCHSIA_BUILD_RUST_DIR=$(dirname "$0")
FUCHSIA_ROOT=$(cd $FUCHSIA_BUILD_RUST_DIR/../..; pwd)
case "$(uname -s)" in
  Linux*) OS=linux-x64;;
  Darwin*) OS=mac-x64;;
  *) echo "Error: unrecognized OS"; exit 1;;
esac
$FUCHSIA_ROOT/buildtools/$OS/rust/bin/cargo run vendor\
  --manifest-path third_party/rust-mirrors/cargo-vendor/Cargo.toml \
  -- --sync $FUCHSIA_ROOT/garnet/Cargo.toml $FUCHSIA_ROOT/third_party/rust-crates/vendor
python $FUCHSIA_ROOT/scripts/rust/check_rust_licenses.py --directory $FUCHSIA_ROOT/third_party/rust-crates/vendor
