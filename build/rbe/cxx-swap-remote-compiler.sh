#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

script="$0"
script_basename="$(basename "$script")"
script_dir="$(dirname "$script")"

function msg() {
  echo "[$script_basename]: $@"
}

# project_root is the same as the remote execution 'exec_root',
# which typically points to a source checkout.
project_root="$(readlink -f "$script_dir"/../..)"

function usage() {
cat <<EOF
$script compiler command...

This wrapper script rewrites and executes a remote compilation commmand.
This script allows a command to use host platform tools locally while
using different platform binaries remotely.
This script itself is only intended to be run in a remote environment.

Responsibility for uploading remote platform tools lies outside of this script.

Pass this script as: rewrapper --remote_wrapper=$script
EOF
}

# realpath doesn't ship with Mac OS X (provided by coreutils package).
# We only want it for calculating relative paths.
# Work around this using Python.
if which realpath 2>&1 > /dev/null
then
  function relpath() {
    local -r from="$1"
    local -r to="$2"
    # -s: preserve symlinks, do not follow them
    # We want rewrapper to treat symlinks as inputs and set them up remotely.
    realpath -s --relative-to="$from" "$to"
  }
else
  # Point to our prebuilt python3.
  python="$(ls "$project_root"/prebuilt/third_party/python3/*/bin/python3)" || {
    echo "*** Python interpreter not found under $project_root/prebuilt/third_party/python3."
    exit 1
  }
  function relpath() {
    local -r from="$1"
    local -r to="$2"
    "$python" -c "import os; print(os.path.relpath('$to', start='$from'))"
  }
fi

project_root_rel="$(relpath . "$project_root")"

# System headers were uploaded from the host toolchain's path.
# We assume that headers and target libs are usable from
# the remote toolchain's package, and structured the same way.
# Ideally, such files should be identical.
(
  # This directory contains subdirs like mac-x64, linux-x64.
  cd "$project_root_rel"/prebuilt/third_party/clang
  for platform in *
  do
    case "$platform" in
      linux-x64) dst="$platform" ;;
      *) src="$platform" ;;
    esac
  done
  # linux-x64 exists because it holds bin/clang and bin/clang++
  cd "$dst"
  ln -s ../"$src"/include .
  ln -s ../"$src"/lib .
)

remote_command=()
for tok
do
  remote_only_tok="$tok"

  case "$tok" in
  */third_party/clang/*/bin/*)
    remote_only_tok="${tok/third_party\/clang\/*\/bin/third_party/clang/linux-x64/bin}"
    ;;
  */third_party/gcc/*/bin/*)
    remote_only_tok="${tok/third_party\/gcc\/*\/bin/third_party/gcc/linux-x64/bin}"
    ;;
  esac

  remote_command+=( "$remote_only_tok" )
done

"${remote_command[@]}"
status="$?"

test "$status" = 0 ||
  msg "substituted remote command failed (exit=$status): ${remote_command[@]}"

exit "$status"
