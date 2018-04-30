#!/bin/bash
set -e
FUCHSIA_BUILD_RUST_DIR=$(dirname "$0")
FUCHSIA_ROOT=$(cd $FUCHSIA_BUILD_RUST_DIR/../..; pwd)
case "$(uname -s)" in
  Linux*) OS=linux-x64;;
  Darwin*) OS=mac-x64;;
  *) echo "Error: unrecognized OS"; exit 1;;
esac
export RUSTC=$FUCHSIA_ROOT/buildtools/$OS/rust/bin/rustc
(cd $FUCHSIA_ROOT; $FUCHSIA_ROOT/buildtools/$OS/rust/bin/cargo run \
  --manifest-path $FUCHSIA_ROOT/third_party/rust-mirrors/cargo-vendor/Cargo.toml \
  -- vendor --sync $FUCHSIA_ROOT/garnet/Cargo.toml $FUCHSIA_ROOT/third_party/rust-crates/vendor)
python $FUCHSIA_ROOT/scripts/rust/check_rust_licenses.py --directory $FUCHSIA_ROOT/third_party/rust-crates/vendor
