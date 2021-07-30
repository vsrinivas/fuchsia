#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [[ ! -d "${FUCHSIA_BUILD_DIR}" ]]; then
  echo "FUCHSIA_BUILD_DIR environment variable not a directory; are you running under fx exec?" 1>&2
  exit 1
fi

BANJO_DIR="$FUCHSIA_DIR/src/devices/tools/fidlgen_banjo/"
FIDL_IR_FILE="${FUCHSIA_BUILD_DIR}/fildgen_banjo_test_ir.json"
FIDL_FILES="$BANJO_DIR/tests/fidl"
C_FILES="$BANJO_DIR/tests/c"
CPP_FILES="$BANJO_DIR/tests/cpp"
RUST_FILES="$BANJO_DIR/tests/rust"
AST_FILES="$BANJO_DIR/tests/ast"
FIDLGEN_BANJO="$FUCHSIA_BUILD_DIR/host_x64/exe.unstripped/fidlgen_banjo"
FIDLC="$FUCHSIA_BUILD_DIR/host_x64/exe.unstripped/fidlc"

FILE="$1"

for f in $FIDL_FILES/*
do
  filename=$(basename -- "$f")
  extension="${filename##*.*.}"
  filename="${filename%.*.*}"

  if [ ! -z  "$FILE" ] && [ "$filename" != "$FILE" ]; then
    continue
  fi

  if [ "$extension" != "fidl" ]; then
    continue
  fi

  if [ "$filename" = "badtype" ]; then
    continue
  fi

  if [ "$filename" = "librarypart_two" ]; then
    continue
  fi

  dependencies=""
  with_c=true
  with_cpp=true
  with_cpp_mock=false
  with_rust=true

  if [ "$filename" = "callback" ] || [ "$filename" = "simple" ] || [ "$filename" = "interface" ] \
    || [ "$filename" = "protocolbase" ] || [ "$filename" = "api" ] \
    || [ "$filename" = "passcallback" ] || [ "$filename" = "fidlhandle" ] \
    || [ "$filename" = "handles" ] || [ "$filename" = "protocolarray" ] \
    || [ "$filename" = "protocolvector" ] || [ "$filename" = "tables" ] \
    || [ "$filename" = "example9" ] || [ "$filename" = "protocolhandle" ] \
    || [ "$filename" = "types" ] || [ "$filename" = "order4" ] || [ "$filename" = "order5" ]; then
    dependencies="$dependencies --files $FUCHSIA_DIR/zircon/vdso/rights.fidl $FUCHSIA_DIR/zircon/vdso/zx_common.fidl"
  fi

  if [ "$filename" = "order6" ]; then
    dependencies="$dependencies --files $FIDL_FILES/order7.test.fidl"
  fi

  if [ "$filename" = "view" ]; then
    dependencies="$dependencies --files $FIDL_FILES/point.test.fidl"
  fi

  if [ "$filename" = "callback" ]; then
    dependencies="$dependencies --files $FIDL_FILES/callback2.test.fidl"
  fi

  if [ "$filename" = "enums" ] || [ "$filename" = "types" ] || [ "$filename" = "example0" ] \
    || [ "$filename" = "example1" ] || [ "$filename" = "example2" ] \
    || [ "$filename" = "example3" ] || [ "$filename" = "alignment" ] \
    || [ "$filename" = "example8" ] || [ "$filename" = "point" ] \
    || [ "$filename" = "tables" ]; then
    with_cpp=false
  fi

  if [ "$filename" = "passcallback" ] || [ "$filename" = "protocolarray" ] \
    || [ "$filename" = "protocolbase" ] || [ "$filename" = "protocolhandle" ] \
    || [ "$filename" = "protocolothertypes" ] || [ "$filename" = "protocolprimitive" ] \
    || [ "$filename" = "protocolvector" ] || [ "$filename" = "interface" ]; then
    with_cpp_mock=true
  fi

  if [ "$filename" = "rustderive" ]; then
    with_c=false
    with_cpp=false
  fi

  if [ "$filename" = "parameterattributes" ]; then
    with_rust=false
    with_c=false
    with_cpp=false
  fi

  if [ "$filename" = "references" ] || [ "$filename" = "buffer" ] || [ "$filename" = "handles" ]; then
    with_rust=false
  fi

  if [ "$filename" = "constants" ] || [ "$filename" = "order" ] || [ "$filename" = "union" ] \
    || [ "$filename" = "order1" ] || [ "$filename" = "order2" ] || [ "$filename" = "order3" ] \
    || [ "$filename" = "order4" ] || [ "$filename" = "order5" ] || [ "$filename" = "order6" ] \
    || [ "$filename" = "order7" ]; then
    with_cpp=false
    with_rust=false
  fi

  echo "Regenerating $filename"
  $FIDLC --experimental new_syntax_only --json "${FIDL_IR_FILE}" $dependencies --files $f
  if [ $with_c = true ]; then
    $FIDLGEN_BANJO --backend C --output "$C_FILES/$filename.h" --ir "${FIDL_IR_FILE}"
  fi
  if [ $with_cpp = true ]; then
    $FIDLGEN_BANJO --backend cpp --output "$CPP_FILES/$filename.h" --ir "${FIDL_IR_FILE}"
    $FIDLGEN_BANJO --backend cpp_internal --output "$CPP_FILES/$filename-internal.h" --ir "${FIDL_IR_FILE}"
  fi
  if [ $with_cpp_mock = true ]; then
    $FIDLGEN_BANJO --backend cpp_mock --output "$CPP_FILES/mock-$filename.h" --ir "${FIDL_IR_FILE}"
  fi
  if [ $with_rust = true ]; then
    $FIDLGEN_BANJO --backend rust --output "$RUST_FILES/$filename.rs" --ir "${FIDL_IR_FILE}"
  fi
done
