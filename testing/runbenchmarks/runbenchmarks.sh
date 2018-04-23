#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script defines a set of utility functions for running benchmarks on
# Fuchisa.  For example usage, see the package //garnet/bin/benchmarks. For
# detailed instructions see README.md.

# Enable stricter error handling:
# - e  Fast fail on errors.
# - u  Fail when attempting to use undefined variables.
set -ue

# Whether any errors occurred while running benchmarks.
_got_errors=0

# Runs a command and expects a results file to be produced.
#
# If the results file was not generated, an error message is logged and a
# benchmark failure is recorded.
#
# @param {string} The expected results file.
# @param {...} Set of arguments that make up the command-to-run.
#
# Example:
#
#    runbench_exec "$OUT_DIR/my_benchmark" trace record  \
#        --spec-file=/path/to/my_benchmark.tspec         \
#        --benchmark-results-file="$OUT_DIR/my_benchmark"
#
runbench_exec() {
    local results_file="$1"

    shift
    _runbench_log "running $@"
    "$@" || _got_errors=1

    _verify_file_exists "${results_file}"
}

# Exits the current process with a code indicating whether any errors occurred.
runbench_exit() {
    exit $_got_errors
}

# Verifies that a regular file exists.
#
# If the file is missing, an error message is logged.
#
# @param {string} Path to the file.
#
# Example: _verify_file_exists "my_file.txt"
_verify_file_exists() {
    local expected_file="$1"
    if [ ! -f "${expected_file}" ]; then
        _got_errors=1
        _runbench_log "missing file ${expected_file}"
    fi
}

# Logs a message to stderr.
_runbench_log() {
    echo "runbench: $@" >&2
}
