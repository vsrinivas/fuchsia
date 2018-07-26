#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script defines a set of utility functions for running benchmarks on
# Fuchisa.  For example usage, see the package //garnet/tests/benchmarks. For
# detailed instructions see README.md.

# Enable stricter error handling:
# - e  Fast fail on errors.
# - u  Fail when attempting to use undefined variables.
#
# This is a Dash script, and does not use the same syntax as Bash. See
# https://wiki.ubuntu.com/DashAsBinSh for more info.
set -ue

# Whether any errors occurred while running benchmarks.
_got_errors=0

# A string of JSON objects representing benchmark results.
#
# The contents of this string are written to a `summary.json` file after all
# benchmarks have run.  Infra uses this file when running benchmarks on hardware
# as a sort of manifest.  It indicates which tests ran, where their output files
# are located, and whether a test passed or failed.  `runbench_exec` records
# each run in this summary.  The summary's schema is defined at:
# https://fuchsia.googlesource.com/zircon/+/master/system/uapp/runtests/summary-schema.json
_benchmark_summaries=""

# Parses command line arguments.  This sets ${OUT_DIR} to the output
# directory specified on the command line, and it reads other arguments,
# which are used by the Catapult converter.
#
# This will normally be invoked as:
#   runbench_read_arguments "$@"
#
# A script that uses runbench_read_arguments should be invoked as follows:
#   benchmarks.sh <output-dir> --catapult-converter-args <args>
runbench_read_arguments() {
    if [ $# -lt 2 ] || [ "$2" != "--catapult-converter-args" ]; then
        echo "Error: Missing '--catapult-converter-args' argument"
        echo "Usage: $0 <output-dir> --catapult-converter-args <args>"
        exit 1
    fi
    OUT_DIR="$1"
    shift 2
    _catapult_converter_args="$@"
}

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

    # Ensure the results file doesn't already exist.
    rm -f "${results_file}"

    # Create the results file so that it's always present, even if the test
    # crashes. Infra expects this file to always be present because it is
    # listed in summary.json.
    touch "${results_file}"

    # Run the benchmark.
    _runbench_log "running $@"
    if ! "$@"; then
        # Record that an error occurred, globally.
        _got_errors=1
        # Record that an error occurred for this particular benchmark.
        _write_summary_entry "${results_file}" "FAIL"
        return
    fi

    # Ensure the output file was actually generated.
    if [ ! -f "${results_file}" ]; then
        _runbench_log "error: missing file ${results_file}"
        _got_errors=1
        _write_summary_entry "${results_file}" "FAIL"
        return
    fi

    # Record a successful run.
    _write_summary_entry "${results_file}" "PASS"

    # Convert the results file to a Catapult Histogram JSON file.
    local base_name="$(basename ${results_file} .json).catapult_json"
    local catapult_file="$(dirname ${results_file})/$base_name"
    /pkgfs/packages/catapult_converter/0/bin/app \
        --input ${results_file} \
        --output ${catapult_file} \
        ${_catapult_converter_args}
    _write_summary_entry "${catapult_file}" "PASS"
}

# Exits the current process with a code indicating whether any errors occurred.
#
# Additionally writes a summary.json file to the given output directory, which
# lists all of the benchmarks that ran, along with their results.  This should
# be the same output directory that results files have been written to.
#
# @param{string} Path to the output directory.
#
# Example: runbench_finish /path/to/dir
runbench_finish() {
    local out_dir="$1"
    _write_summary_file "${out_dir}"

    exit $_got_errors
}

# Logs a message to stderr.
_runbench_log() {
    echo "runbench: $@" >&2
}

# Records the result of running a benchmark in the summary.
#
# @param {string} Path to the results file.
# @param {string} Either "PASS" or "FAIL".
#
# Example: _write_summary_entry /path/to/res/file "PASS"
_write_summary_entry() {
    local results_file="$1"
    local pass_fail="$2"

    # Build the JSON summary entry.  The name of the results file is used as
    # `name` and `output_file`.  `name` must be unique with the summary.
    # Since results file names also must be unique, using the filename is fine.
    # `output_file` must be a path relative to summary.json.  We'll emit
    # summary.json in the same directory as results files, so the filename is
    # also fine to use here.

    # Extract the last element of the pathname, e.g. "cc" from "aa/bb/cc"
    local results_filename="${results_file##*/}"

    # Create the JSON fields.
    local name="\"name\":\"${results_filename}\""
    local output_file="\"output_file\":\"${results_filename}\""
    local result="\"result\":\"${pass_fail}\""

    # Append the entry to the summary string.
    _benchmark_summaries="${_benchmark_summaries}{${name},${output_file},${result}},\n"
}

# Writes summary.json in the given directory.
#
# @param{string} Path to the directory.
#
# Example: _write_summary_file /path/to/dir
_write_summary_file() {
    local out_dir="$1"
    local summary_filepath="${out_dir}/summary.json"

    # Strip trailing ",\n"
    _benchmark_summaries=${_benchmark_summaries%,\\n}
    local summary="{\"tests\":[\n${_benchmark_summaries}\n]}"

    _runbench_log "writing summary.json to ${summary_filepath}"
    echo "${summary}" > "${summary_filepath}"
}
