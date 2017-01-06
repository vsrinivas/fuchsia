This directory holds tests for the modular framework.

Tests here are integration tests, run against a fully built system on
either build host (using QEMU) or on a target device.

Right now the only test available is the parent_child test, which
can be invoked via `parent_child/test.sh` after setting up the
environment by sourcing `scripts/env.sh` and running `fset`.
