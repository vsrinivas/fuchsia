#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All Rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script for measuring action tracing build overhead.
# See usage() below.

set -Eeu
set -o pipefail

# Constants:

# These files will be locally modified for this experiment:
# Assume default output dir for now.
readonly args_gn=out/default/args.gn
readonly bcfg_gn=build/config/BUILDCONFIG.gn

# The `time` binary has more options than the shell built-in.
time=/usr/bin/time

# Where locally patched files and logs will be saved.
date="$(date +%Y.%m.%d.%H.%M.%S)"
readonly date
exp_dir="$(mktemp -t -d tmp.action_trace_overhead.$date.XXXX)"
readonly exp_dir

function usage() {
  cat <<EOF
$0 N

This script measures the overhead of action-tracing.
Run this from \$FUCHSIA_DIR, your source checkout root.

N: (positional) run the experiment this many times.

This makes temporary changes to the following files,
but will restore them after all is done:
  $args_gn
  $bcfg_gn

This leaves all others settings in args.gn, e.g. goma, untouched.
Temporary and log files will be kept in $exp_dir (example).
EOF
}

# Expect one positional argument, print usage() otherwise.
test "$#" = 1 || { usage; exit 1;}
N="$1"

# Disable tracing by patching the args.gn file.
function disable_trace() {
  local _args_gn="$1"
  if grep -q -w build_should_trace_actions "$_args_gn"
  then
    sed -e '/build_should_trace_actions/s|true|false|' "$_args_gn"
  else
    cat "$_args_gn"
    echo "build_should_trace_actions = false"
  fi
}

# Enable tracing by patching the args.gn file.
function enable_trace() {
  local _args_gn="$1"
  if grep -q -w build_should_trace_actions "$_args_gn"
  then
    sed -e '/build_should_trace_actions/s|false|true|' "$_args_gn"
  else
    cat "$_args_gn"
    echo "build_should_trace_actions = true"
  fi
}

function disable_checks() {
  local _bcfg_gn="$1"
  # Ignore trace errors, to let the build continue past trace errors.
  # Force --no-check-access-permissions.
  sed -e 's|--failed-check-status=1|--failed-check-status=0|' \
    -e '/if (!defined(hermetic_deps))/,/}/s|hermetic_deps = true|hermetic_deps = false|' \
    "$_bcfg_gn"
}

function enable_checks() {
  local _bcfg_gn="$1"
  # Ignore trace errors, to let the build continue past trace errors.
  # Let --check-access-permissions run by default,
  # disregarding suppressions.
  sed -e 's|--failed-check-status=1|--failed-check-status=0|' \
    -e '/if (!defined(hermetic_deps))/,/}/s|hermetic_deps = false|hermetic_deps = true|' \
    "$_bcfg_gn"
}

# 1. Generate experiment configurations.
# Backup temporarily modified files
echo "Using temporary directory for local changes and logs: $exp_dir"
mkdir -p "$exp_dir"/"$(dirname "$args_gn")"
mkdir -p "$exp_dir"/"$(dirname "$bcfg_gn")"
cp "$args_gn" "$exp_dir"/"$args_gn".orig
cp "$bcfg_gn" "$exp_dir"/"$bcfg_gn".orig

# Keep these around so we can diff them easily.
disable_trace "$exp_dir"/"$args_gn".orig > "$exp_dir"/"$args_gn.notrace"
enable_trace "$exp_dir"/"$args_gn".orig > "$exp_dir"/"$args_gn.trace"

# Configure with checks enabled/disabled, ignoring suppressions.
disable_checks "$exp_dir"/"$bcfg_gn".orig > "$exp_dir"/"$bcfg_gn".nochecks
enable_checks "$exp_dir"/"$bcfg_gn".orig > "$exp_dir"/"$bcfg_gn".allchecks

function vdiff() {
  echo "diff -u $1 $2"
  diff -u "$1" "$2" || :
}

function vcat() {
  echo ""
  echo "<<< $1 <<<"
  cat "$1"
  echo ">>> $1 >>>"
}

vdiff "$exp_dir"/"$args_gn".{orig,notrace}
vdiff "$exp_dir"/"$args_gn".{orig,trace}
vdiff "$exp_dir"/"$bcfg_gn".{orig,nochecks}
vdiff "$exp_dir"/"$bcfg_gn".{orig,allchecks}

# Benchmark build one time.
function run_once() {
  local log="$1"
  # do `fx clean-build`, but only time the build part.
  fx clean
  # time the build and ignore any build errors
  "$time" -a --output="$log" fx build || :
}

### Run benchmarks.
function run_all() {
  local N="$1"
  echo "=== Running all configurations ==="

  echo "===== Benchmarking baseline, with no tracing ====="
  cp "$exp_dir"/"$args_gn".notrace "$args_gn"
  cp "$exp_dir"/"$bcfg_gn".nochecks "$bcfg_gn"
  fx gen
  rm -f build.time.notrace
  for i in $(seq $N); do
    echo "======= run $i ======="
    run_once "$exp_dir"/build.time.notrace
  done

  echo "===== Benchmarking tracing, all checks disabled ====="
  # 2. Measure with tracing, but no checks
  cp "$exp_dir"/"$args_gn".trace "$args_gn"
  cp "$exp_dir"/"$bcfg_gn".nochecks "$bcfg_gn"
  fx gen
  rm -f build.time.trace.nochecks
  for i in $(seq $N); do
    echo "======= run $i ======="
    run_once "$exp_dir"/build.time.trace.nochecks
  done

  echo "===== Benchmarking tracing, all checks enabled ====="
  cp "$exp_dir"/"$args_gn".trace "$args_gn"
  cp "$exp_dir"/"$bcfg_gn".allchecks "$bcfg_gn"
  fx gen
  rm -f build.time.trace.allchecks
  for i in $(seq $N); do
    echo "======= run $i ======="
    run_once "$exp_dir"/build.time.trace.allchecks
  done

  cat <<EOF
=== All done. ===
Files are in: $exp_dir
Test configurations are saved in:

  $exp_dir/$args_gn.notrace
  $exp_dir/$args_gn.trace

  $exp_dir/$bcfg_gn.nochecks
  $exp_dir/$bcfg_gn.allchecks

Raw time logs are in:
  $exp_dir/build.time.notrace (baseline, no tracing)
  $exp_dir/build.time.trace.nochecks (traced, no checks)
  $exp_dir/build.time.trace.allchecks (traced, all checks)

EOF
  echo "== Summary =="
  {
    vcat $exp_dir/"$args_gn".orig
    vcat $exp_dir/build.time.notrace
    vcat $exp_dir/build.time.trace.nochecks
    vcat $exp_dir/build.time.trace.allchecks
  } | grep -v pagefaults | \
    sed -e '/CPU/s/\(user\|system\|elapsed\|\%CPU\)/ \1/g' \
      -e 's|CPU.*|CPU|'
}

function restore_files() {
  echo "=== Restoring original configuration. ==="
  # Temporary files will still be left around for examination.
  cp "$exp_dir/$args_gn".orig "$args_gn"
  cp "$exp_dir/$bcfg_gn".orig "$bcfg_gn"
}

trap restore_files EXIT

run_all "$N"

