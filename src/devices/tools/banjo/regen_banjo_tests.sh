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
AST_FILES="$BANJO_DIR/test/ast"
BANJO_BIN="$FUCHSIA_BUILD_DIR/host_x64/banjo_bin"

for f in $BANJO_FILES/*
do
  filename=$(basename -- "$f")
  extension="${filename##*.*.}"
  filename="${filename%.*.*}"

  if [ "$filename" = "library_part_two" ]; then
    continue
  fi
  if [ "$filename" = "library_part_one" ]; then
    $BANJO_BIN --backend ast --omit-zx --output "$AST_FILES/library_parts.test.ast" --files $f "$BANJO_FILES/library_part_two.test.banjo"
    continue
  fi

  dependencies=""
  zx="--omit-zx"
  with_c=true
  with_cpp=true
  with_cpp_mock=false
  with_rust=true

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
    with_cpp=false
  fi

  if [ "$filename" = "pass-callback" ] || [ "$filename" = "protocol-array" ] \
    || [ "$filename" = "protocol-base" ] || [ "$filename" = "protocol-handle" ] \
    || [ "$filename" = "protocol-other-types" ] || [ "$filename" = "primitive" ] \
    || [ "$filename" = "protocol-vector" ]; then
    with_cpp_mock=true
  fi

  if [ "$filename" = "rust-derive" ]; then
    with_c=false
    with_cpp=false
  fi

  if [ "$filename" = "parameter-attributes" ]; then
    with_rust=false
    with_c=false
    with_cpp=false
  fi

  if [ "$filename" = "references" ] || [ "$filename" = "buffer" ]; then
    with_rust=false
  fi

  if [ "$filename" = "constants" ]; then
    with_cpp=false
    with_rust=false
  fi

  echo "Regenerating $filename"
  if [ $with_c = true ]; then
    $BANJO_BIN --backend C $zx --output "$C_FILES/$filename.h" $dependencies --files $f
  fi
  if [ $with_cpp = true ]; then
    $BANJO_BIN --backend cpp $zx --output "$CPP_FILES/$filename.h" $dependencies --files $f
    $BANJO_BIN --backend cpp_i $zx --output "$CPP_FILES/$filename-internal.h" $dependencies --files $f
  fi
  if [ $with_cpp_mock = true ]; then
    $BANJO_BIN --backend cpp_mock $zx --output "$CPP_FILES/mock-$filename.h" $dependencies --files $f
  fi
  if [ $with_rust = true ]; then
    $BANJO_BIN --backend rust $zx --output "$RUST_FILES/$filename.rs" $dependencies --files $f
  fi
  $BANJO_BIN --backend ast $zx --output "$AST_FILES/$filename.test.ast" $dependencies --files $f
done
