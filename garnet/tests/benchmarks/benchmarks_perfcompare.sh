#!/boot/bin/sh
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script is used for the performance comparison (perfcompare) CQ
# bots, which compare performance before and after a change.
#
# This script runs a subset of benchmarks for the Garnet layer.  It
# runs a subset of what benchmarks.sh runs.  The reason for running a
# subset is that the full set of tests currently takes too long and
# tends to exceed the bot timeout.
#
# For usage, see runbench_read_arguments in runbenchmarks.sh.

# Import the runbenchmarks library.
. /pkgfs/packages/runbenchmarks/0/data/runbenchmarks.sh

runbench_read_arguments "$@"

# Performance tests implemented in the Zircon repo.
runbench_exec "${OUT_DIR}/zircon.perf_test.json" \
    /pkgfs/packages/garnet_benchmarks/0/test/sys/perf-test \
    -p --out="${OUT_DIR}/zircon.perf_test.json"

# Performance tests implemented in the Garnet repo (the name
# "zircon_benchmarks" is now misleading).
runbench_exec "${OUT_DIR}/zircon_benchmarks.json" \
    /pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks \
    -p --out="${OUT_DIR}/zircon_benchmarks.json"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
