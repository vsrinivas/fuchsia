#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# See usage() for description.

script="$0"
script_dir="$(dirname "$script")"

# The project_root must cover all inputs, prebuilt tools, and build outputs.
# This should point to $FUCHSIA_DIR for the Fuchsia project.
# ../../ because this script lives in build/rbe.
# The value is an absolute path.
project_root="$(readlink -f "$script_dir"/../..)"

build_subdir="$(realpath --relative-to="$project_root" . )"
project_root_rel="$(realpath --relative-to=. "$project_root")"

function usage() {
  cat <<EOF
This wrapper script validates a command in the following manner:

Reject any occurrences of the output dir ($build_subdir):

  1) in the command's tokens
  2) in the output files' paths
  3) in the output files' contents

Usage: $script output_files... -- command...
EOF
}

function error_msg() {
  echo "[$script] Error: " "$@"
}

command_break=0
# Collect names of output files.
outputs=()
for opt
do
  case "$opt" in
    -h | --help) usage; exit ;;
    --) command_break=1; shift; break ;;
    *) outputs+=( "$opt" ) ;;
  esac
  shift
done

test "$command_break" = 1 || {
  error_msg "Missing -- before command."
  usage
  exit 1
}

# Scan outputs' paths
err=0
for f in "${outputs[@]}"
do
  case "$f" in
    *"$build_subdir"* )
      err=1
      error_msg "Output path '$f' contains '$build_subdir'"
      ;;
  esac
done

# Command is in "$@".  Scan its tokens for $build_dir.
for tok
do
  case "$tok" in
    *"$build_subdir"* )
      err=1
      error_msg "Command token '$tok' contains '$build_subdir'"
      ;;
  esac
  # Do not shift, keep tokens for execution.
done

# Run the command.
"$@"

status="$?"

# On success, check the outputs.
test "$status" != 0 || {
  for f in "${outputs[@]}"
  do
    if grep -q "$build_dir" "$f"
    then
      err=1
      error_msg "Output file $f contains '$build_subdir'"
    fi
  done
  test "$err" = 0 || exit 1
}

exit "$status"
