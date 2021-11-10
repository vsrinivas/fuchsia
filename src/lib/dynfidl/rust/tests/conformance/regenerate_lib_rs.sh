#!/usr/bin/env bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/../../../../../../tools/devshell/lib/vars.sh || exit $?
fx-config-read

TEST_FROM_IR_BIN="${FUCHSIA_BUILD_DIR}/host_x64/dynfidl_conformance_test_from_fidl_ir"
GENERATED_TEST="$FUCHSIA_DIR/src/lib/dynfidl/rust/tests/conformance/src/lib.rs"

# NOTE this path may change if fidling or the conformance suite is refactored
CONFORMANCE_IR="${FUCHSIA_BUILD_DIR}/fidling/gen/src/tests/fidl/conformance_suite/conformance_fidl.fidl.json"

# NOTE change `linux-x64` to `mac-x64` if running on a mac
RUSTFMT="$FUCHSIA_DIR/prebuilt/third_party/rust/linux-x64/bin/rustfmt"

# only rebuild the generator binary so we can run the script when the tests don't build
fx-command-run build host_x64/dynfidl_conformance_test_from_fidl_ir || ( \
  fx-error "Failed to build."; \
  exit 1
)

(cd "$FUCHSIA_DIR"; "$TEST_FROM_IR_BIN" \
  --input "$CONFORMANCE_IR" \
  --output "$GENERATED_TEST" \
  --rustfmt "$RUSTFMT" \
  conformance/EmptyStruct \
  conformance/FidlvizStruct1 \
  conformance/FidlvizStruct2 \
  conformance/FiveByte \
  conformance/GoldenBoolStruct \
  conformance/GoldenIntStruct \
  conformance/GoldenUintStruct \
  conformance/Int64Struct \
  conformance/MyBool \
  conformance/MyByte \
  conformance/MyInt16 \
  conformance/MyInt32 \
  conformance/MyInt64 \
  conformance/MyInt8 \
  conformance/MyUint16 \
  conformance/MyUint32 \
  conformance/MyUint64 \
  conformance/MyUint8 \
  conformance/NodeAttributes \
  conformance/PaddingBetweenFieldsInt16Int32 \
  conformance/PaddingBetweenFieldsInt16Int64 \
  conformance/PaddingBetweenFieldsInt32Int64 \
  conformance/PaddingBetweenFieldsInt8Int16 \
  conformance/PaddingBetweenFieldsInt8Int32 \
  conformance/PaddingBetweenFieldsInt8Int64 \
  conformance/Regression1 \
  conformance/Size5Alignment4 \
  conformance/Size8Align8 \
  conformance/Struct1Byte \
  conformance/Struct2Byte \
  conformance/Struct3Byte \
  conformance/Struct4Byte \
  conformance/Struct5Byte \
  conformance/Struct6Byte \
  conformance/Struct7Byte \
  conformance/Struct8Byte \
  conformance/StructSize16Align8 \
  conformance/StructSize3Align2 \
  conformance/StructWithInt \
  conformance/ThreeByte \
  conformance/TransformerEmptyStruct \
  conformance/Uint16Struct \
  conformance/Uint32Struct \
  conformance/Uint64Struct \
  conformance/Uint64Uint32Uint16Uint8 \
  conformance/Uint8Struct \
  conformance/Uint8Uint16Uint32Uint64)
