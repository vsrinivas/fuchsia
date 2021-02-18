#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function usage() {
  cat <<EOF
usage: $0 DIR1 DIR2

Compares two build directories for artifact differences.
Comparison logic tries to account for the file type in
choosing a diff strategy.

Run this from the Fuchsia source checkout root dir ($FUCHSIA_DIR),
because it references some tools from the source tree.

Example: Compare two clean builds with same output dir:
  fx set ...
  fx clean-build
  cp -p -r out/default out/default.bkp
  fx clean-build
  $0 out/default.bkp out/default

EOF
}

# This JSON formatter only overwrites the file in place.
# Usage: json_format --format FILE
readonly json_format=third_party/catapult/telemetry/json_format

# GLOBAL MUTABLE
# Accumulate paths to unexpected differences here.
# bash: array variables are not POSIX
unexpected_diffs=()

function diff_json() {
  # json_format doesn't have an option to output to stdout,
  # so we must copy it to temporary files.
  cp "$1"{,.formatted}
  cp "$2"{,.formatted}
  "$json_format" --format "$1".formatted
  "$json_format" --format "$2".formatted
  diff -u "$1".formatted "$2".formatted
  # return with the exit code of diff
}

function diff_text() {
  diff -u "$1" "$2"
}

function diff_binary() {
  diff -q "$1" "$2"
}

function diff_file_relpath() {
  # $1 is left dir
  # $2 is right dir
  # $3 is common path under both dirs, and is itself not a directory.
  # one could also use an all-inclusive diff tool like https://diffoscope.org/
  local left="$1/$3"
  local right="$2/$3"
  common_path="$3"
  filebase="$(basename "$common_path")"

  # TODO(fangism): Some files are stored as blobs so content differences
  # appear as filename entry differences.  Skip these.  Perhaps silently?
  if test ! -f "$left"
  then printf "%s does not exist\n" "$left"
    return
  fi
  if test ! -f "$right"
  then echo "%s does not exist\n" "$right"
    return
  fi

  # Add known diff cases below
  # Goal:
  #   * Identify and classify known differences.
  #   * Gradually reduce sources of differences.
  case "$filebase" in
    # The exit status of this case statement will be used
    # to determine whether or not the given file is an
    # erroneous diff.
    #
    # Generally:
    #   diff_text for text files that are expected to match
    #   diff_binary for binaries or known large textual differences

    # depfiles
    *.d) diff_text "$left" "$right" ;;

    # C++ object files (binary)
    *.o) diff_binary "$left" "$right" ;;
    # TODO(fangism): compare objdumps for details
    # TODO(fangism): suppress known issues on a path-by-path basis

    # Ignore .a differences until .o differences have been eliminated.
    *.a) diff_binary "$left" "$right" || : ;;

    # Rust libraries (binary)
    *.rlib) diff_binary "$left" "$right" ;;

    # The following groups of files have known huge diffs,
    # so omit details from the general report, and diff_binary.
    # For now, ignore these differences (using || :) until their
    # inputs' differences have been resolved and eliminated.
    meta.far) diff_binary "$left" "$right" || : ;;
    meta.far.merkle) diff_binary "$left" "$right" || : ;;
    contents) diff_binary "$left" "$right" || : ;;
    blobs.json) diff_binary "$left" "$right" || : ;;
    blobs.manifest) diff_binary "$left" "$right" || : ;;
    package_manifest.json) diff_binary "$left" "$right" || : ;;
    targets.json) diff_binary "$left" "$right" || : ;;

    # Diff formatted JSON for readability.
    *.json) diff_json "$left" "$right" ;;

    # Ignore ninja logs, as they bear timestamps,
    # and are non-essential build artifacts.
    .ninja.log) ;;

    # Ignore filesystem access trace files.
    # They may contain nondeterministic paths to /proc/PID
    *_trace.txt) ;;

    # like exe.unstripped/*.map files
    # Many of these (but not all) reference mktemp paths.
    *.map) diff_binary "$left" "$right" ;;

    # Ignore stamp files.
    *.stamp) ;;

    # Ignore temporary and backup files.
    *.tmp) ;;
    *.bak) ;;
    *.bkp) ;;

    # All others.
    # Binary files diffs will still only be reported tersely.
    *) diff_text "$left" "$right" ;;
  esac
  # Record unexpected differences.
  test "$?" = 0 || unexpected_diffs=("${unexpected_diffs[@]}" "$common_path")
}

function diff_dir_recursive() {
  # $1 and $2 are two directories, each two levels deep, e.g. "out/default"
  # For dual-traversal, arbitrarily use $2's subdirs.
  for f in "$2"/*
  do
    relpath="${f#"$2/"}"   # Remove $2/ prefix.
    filebase="$(basename "$relpath")"
    if test -d "$f"
    then diff_dir_recursive "$1/$filebase" "$f"
    else diff_file_relpath "$1" "$2" "$relpath"
    fi
  done
}

test "$#" = 2 || { usage; exit 2; }

diff_dir_recursive "$1" "$2"

# TODO(fangism): summarize findings in a report

if test "${#unexpected_diffs[@]}" != 0
then
  echo "UNEXPECTED DIFFS:"
  for path in "${unexpected_diffs[@]}"
  do echo "  $path"
  done
  exit 1
fi

# TODO(fangism): Track unexpected *matches* so that when differences
# are eliminated, their suppressions get removed.
