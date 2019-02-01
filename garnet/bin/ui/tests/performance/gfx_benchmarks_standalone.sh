#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs only the graphics benchmarks. Use only for running the
# benchmarks locally, for development purposes.
#
# For usage, see runbench_read_arguments in runbenchmarks.sh.

# Import the runbenchmarks library.
. /pkgfs/packages/runbenchmarks/0/data/runbenchmarks.sh

runbench_read_arguments "$@"

# Run the gfx benchmarks in the current shell environment, because they write
# to (hidden) global state used by runbench_finish.
. /pkgfs/packages/garnet_benchmarks/0/bin/gfx_benchmarks.sh "$@"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
