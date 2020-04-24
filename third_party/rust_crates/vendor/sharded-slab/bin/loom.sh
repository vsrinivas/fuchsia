#!/bin/bash
# Runs Loom tests with defaults for Loom's configuration values.
#
# The nightly Rust compiler is used to enable Loom's location tracking support.
# This allows Loom to emit diagnostics that include the caller's source code
# locations when errors occur. 
#
# The tests are compiled in release mode to improve performance, but debug
# assertions are enabled. 
#
# Any arguments to this script are passed to the `cargo test` invocation.

RUSTFLAGS="${RUSTFLAGS} --cfg loom_nightly -C debug-assertions=on" \
    LOOM_MAX_PREEMPTIONS="${LOOM_MAX_PREEMPTIONS:-2}" \
    LOOM_CHECKPOINT_INTERVAL="${LOOM_CHECKPOINT_INTERVAL:-1}" \
    LOOM_LOG=1 \
    LOOM_LOCATION=1 \
    cargo +nightly test --release "$@"
