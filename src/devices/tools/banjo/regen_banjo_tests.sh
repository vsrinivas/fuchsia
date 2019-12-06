#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ ! -d "${FUCHSIA_BUILD_DIR}" ]]; then
  echo "FUCHSIA_BUILD_DIR environment variable not a directory; are you running under fx exec?" 1>&2
  exit 1
fi

BANJO_DIR="$FUCHSIA_DIR/src/devices/tools/banjo/"
BANJO_FILES="$BANJO_DIR/test/banjo"
C_FILES="$BANJO_DIR/test/c"
CPP_FILES="$BANJO_DIR/test/cpp"
RUST_FILES="$BANJO_DIR/test/rust"
#BANJO_BIN=${BANJO_BIN:-"$FUCHSIA/zircon/prebuilt/downloads/banjo/banjo_bin"}
BANJO_BIN="$FUCHSIA_BUILD_DIR/host_x64/banjo_bin"

for f in $BANJO_FILES/*
do
  filename=$(basename -- "$f")
  extension="${filename##*.*.}"
  filename="${filename%.*.*}"

  dependencies=""
  zx="--omit-zx"
  type_only=false
  rust_only=false
  if [ "$filename" = "callback" ] || [ "$filename" = "simple" ] || [ "$filename" = "interface" ] \
    || [ "$filename" = "protocol-base" ] ; then
    zx=""
  fi

  if [ "$filename" = "view" ]; then
    dependencies="$dependencies --files $BANJO_FILES/point.test.banjo"
  fi

  if [ "$filename" = "enums" ] || [ "$filename" = "types" ] || [ "$filename" = "example-0" ] \
    || [ "$filename" = "example-1" ] || [ "$filename" = "example-2" ] \
    || [ "$filename" = "example-3" ] || [ "$filename" = "alignment" ] \
    || [ "$filename" = "example-8" ] || [ "$filename" = "point" ] \
    || [ "$filename" = "tables" ]; then
    type_only=true
  fi

  if [ "$filename" = "rust-derive" ]; then
    rust_only=true
  fi

  echo "Regenerating $filename"
  if [ $rust_only = false ]; then
    $BANJO_BIN --backend C $zx --output "$C_FILES/$filename.h" $dependencies --files $f
    if [ $type_only = false ]; then
      $BANJO_BIN --backend cpp $zx --output "$CPP_FILES/$filename.h" $dependencies --files $f
      $BANJO_BIN --backend cpp_i $zx --output "$CPP_FILES/$filename-internal.h" $dependencies --files $f
    fi
  fi
  $BANJO_BIN --backend rust $zx --output "$RUST_FILES/$filename.rs" $dependencies --files $f
done
