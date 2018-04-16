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

# Runs a tracing-based benchmark from a tspec file.
#
# If the results file was not generated, an error message is logged.
#
# @param {string} The file to write results to.
# @param {string} The absolute path to the tspec file.
#
# Example: runbench_trace "my_benchmark" /path/to/my_benchmark.tspec
runbench_trace() {
    local results_file="$1"
    local tspec_file="$2"

    # Run the trace tool.
    trace record --spec-file="${tspec_file}" \
                 --benchmark-results-file="${results_file}" || _got_errors=1

    _verify_file_exists "${results_file}"
}

# Runs a benchmark executable.
#
# If the results file was not generated, an error message is logged.
#
# @param {string} The file to write results to.
# @param {string} The absolute path to the tspec file.
#
# Example: runbench_exec "my_benchmark" /path/to/exec
runbench_exec() {
    local results_file="$1"
    local executable="$2"

    # TODO(kjharland): Specify results file using --results-file= once all
    # existing commands support it.
    $executable "${results_file}" || _got_errors=1

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
        echo "runbench: missing file ${expected_file}" >&2
    fi
}

