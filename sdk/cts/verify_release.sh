#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function usage() {
  cat <<EOF
usage: $0

Generate a CTS archive from your local workspace, extract the contents and build the tests.
CTS archives should not be uploaded to CIPD unless they pass this validation step.

EOF
}

FUCHSIA_DIR="${FUCHSIA_DIR:-$HOME/fuchsia}"
RELEASE_DIR_REL="prebuilt/cts/test"
RELEASE_DIR="$FUCHSIA_DIR/$RELEASE_DIR_REL"
PRODUCT_BOARD="core.x64"

# TODO(jcecil): consider using "auto-dir", which will change the OUT_DIR path.
# https://fuchsia.dev/reference/tools/fx/cmd/set?hl=en
OUT_DIR="out/default"
ARCHIVE="cts.tar.gz"
ARCHIVE_DIR="$FUCHSIA_DIR/$OUT_DIR/sdk/archive"

###############################################################################

function generate_archive() {
  cd $FUCHSIA_DIR
  # `fx set` with the right arguments to build the cts archive.
  fx set $PRODUCT_BOARD --with //sdk:cts --args build_sdk_archives=true
  fx build
}

function build_archive_contents() {
  # Copy the CTS archive into the test release directory
  cd $FUCHSIA_DIR
  mkdir -p $RELEASE_DIR
  cd $RELEASE_DIR
  rm -rf arch BUILD.gn docs examples fidl json meta pkg tests
  cp $ARCHIVE_DIR/$ARCHIVE .
  tar -xvf $ARCHIVE
  rm $ARCHIVE

  # `fx set` with the newly extracted archive tests.
  # Verify they build successfully.
  cd $FUCHSIA_DIR
  fx set core.x64 --with //prebuilt/cts/test:cts
  fx build
}

function run_tests() {
  # TODO
  # Setup emulator using VDL
  # run `fx test`
  echo "Not Implemented"
}

# No arguments are currently supported
if [ $# -gt 0 ]; then
  usage
  exit 1
fi

# Exit if any command fails
set -e
set -o xtrace

generate_archive

build_archive_contents

#run_tests
