#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script wraps imager.py and provides some configuration convenience
# functionality. For example if a directory containing the fuchsia build output
# is not supplied we assume it is two directories up from the build script and
# then in a sub-directory for a given architecture. We also set sensible
# defaults for things like partition size, etc.

set -e -u

# construct the path to our directory
script_name=$(basename "$0")
script_dir=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
expected_path="scripts/installer"
curr_dir=$(pwd)

# check that it looks like the script lives inside a fuchsia source tree
if [ $(dirname "$(dirname "$script_dir")")/"$expected_path" != "$script_dir" ]; then
  echo "It doesn't look like we're running in the right place, please make" \
    "sure the script is in a fuchsia source tree in scripts/installer"
  exit -1
fi

DEFAULT_SIZE_SYSTEM=4
BLOCK_SIZE=1024
STAGING_DIR="${script_dir}/build-installer"
# TODO take a size for the magenta partition as well
blocks=0
release=0
debug=0
platform=""
build_dir=""
minfs_path=""

while getopts ":u:hrdp:b:m:" opt; do
  case $opt in
    u)
      blocks=$(($OPTARG * 1024 * 1024))
      ;;
    h)
      echo "build-installable-usersfs.sh -u <SIZE> [-r|-d] [-p] [-b <BUILD DIR>]"
      echo "-u: size of system partition in GB"
      echo "-r: use the release build directory, should not be used with -d"
      echo "-d: use the debug build directory, should not be used with -r"
      echo "-p: platform architecture, eg. x86-64, arm-32, etc"
      echo "-b: specify the build directory manually, this will cause -r, -d," \
        "and -p arguments to be ignored"
      echo "-m: path to the host architecture minfs binary, perhaps you need" \
        "to run 'make' in magenta/system/uapp/minfs"
      exit 0
      ;;
    r)
      release=1
      ;;
    d)
      debug=1
      ;;
    p)
      platform=$OPTARG
      ;;
    b)
      build_dir=$OPTARG
      ;;
    m)
      minfs_path=$OPTARG
      ;;
    \?)
      echo "Unknown option -$OPTARG"
  esac
done

if [ "$blocks" -eq 0 ]; then
  blocks=$(($DEFAULT_SIZE_SYSTEM * 1024 * 1024))
fi

if [ ! -f "$minfs_path" ]; then
  echo "minfs path not found, please build minfs for your host and supply the" \
    "path"
  exit -1
fi

# if the build directory is not specified, infer it from other parameters
if [ "$build_dir" = "" ]; then
  if [ "$release" -eq "$debug" ]; then
    if [ "$debug" -eq 0 ]; then
      debug=1
    else
      echo "Please choose release or debug, but not both"
      exit -1
    fi
  fi

  if [ "$platform" = "" ]; then
    platform=x86-64
  fi

  build_dir=""
  if [ "$release" -eq 1 ]; then
    build_dir="release"
  else
    build_dir="debug"
  fi

  build_dir=$script_dir/../../out/$build_dir-$platform
else
  if [ "$release" -ne 0 ] || [ "$debug" -ne 0 ] || [ "$platform" -ne "" ]; then
    echo "build directory is specified, platform and release args ignored"
  fi
fi

disk_path="${STAGING_DIR}/user_fs"

if [ ! -d "$build_dir" ]; then
  echo "Output directory '$build_dir' not found, please make sure you've"\
    "supplied the right build type and architecture OR correct path."
  exit -1
fi

if [ ! -d  "$STAGING_DIR" ]; then
  mkdir "$STAGING_DIR"
else
  rm -rf -- "$STAGING_DIR"/*
fi

# create a suitably large file
echo "Creating disk image, this may take some time ${script_name} ..."
dd if=/dev/zero of="$disk_path" bs="$BLOCK_SIZE" count="$blocks"
"$minfs_path" "$disk_path" mkfs

mcpy_loc=$(which mcopy)
lz4_path=$(which lz4)

"${script_dir}"/imager.py --disk_path="$disk_path" --mcp_path="$mcpy_loc" \
  --lz4_path="$lz4_path" --build_dir="$build_dir" --temp_dir="$STAGING_DIR" \
  --minfs_path="$minfs_path"
