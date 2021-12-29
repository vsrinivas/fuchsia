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
  test.conformance/Bounded32NonnullableString \
  test.conformance/Bounded32NonnullableVectorOfUint32s \
  test.conformance/EmptyStruct \
  test.conformance/FidlvizStruct1 \
  test.conformance/FidlvizStruct2 \
  test.conformance/FiveByte \
  test.conformance/GoldenBoolStruct \
  test.conformance/GoldenByteVectorStruct \
  test.conformance/GoldenIntStruct \
  test.conformance/GoldenStringStruct \
  test.conformance/GoldenStringWithMaxSize2 \
  test.conformance/GoldenUintStruct \
  test.conformance/Int64Struct \
  test.conformance/Length2StringWrapper \
  test.conformance/LotsOfVectors \
  test.conformance/MultipleNonnullableStrings \
  test.conformance/MultipleBoundedNonnullableVectorsOfUint32s \
  test.conformance/MultipleNonnullableVectorsOfUint32s \
  test.conformance/MultipleShortNonnullableStrings \
  test.conformance/MultipleShortNullableStrings \
  test.conformance/MyBool \
  test.conformance/MyByte \
  test.conformance/MyInt16 \
  test.conformance/MyInt32 \
  test.conformance/MyInt64 \
  test.conformance/MyInt8 \
  test.conformance/MyUint16 \
  test.conformance/MyUint32 \
  test.conformance/MyUint64 \
  test.conformance/MyUint8 \
  test.conformance/NodeAttributes \
  test.conformance/PaddingBetweenFieldsInt16Int32 \
  test.conformance/PaddingBetweenFieldsInt16Int64 \
  test.conformance/PaddingBetweenFieldsInt32Int64 \
  test.conformance/PaddingBetweenFieldsInt8Int16 \
  test.conformance/PaddingBetweenFieldsInt8Int32 \
  test.conformance/PaddingBetweenFieldsInt8Int64 \
  test.conformance/Regression1 \
  test.conformance/Size5Alignment4 \
  test.conformance/Size8Align8 \
  test.conformance/StringWrapper \
  test.conformance/Struct1Byte \
  test.conformance/Struct2Byte \
  test.conformance/Struct3Byte \
  test.conformance/Struct4Byte \
  test.conformance/Struct5Byte \
  test.conformance/Struct6Byte \
  test.conformance/Struct7Byte \
  test.conformance/Struct8Byte \
  test.conformance/StructSize16Align8 \
  test.conformance/StructSize3Align2 \
  test.conformance/StructWithInt \
  test.conformance/ThreeByte \
  test.conformance/TransformerEmptyStruct \
  test.conformance/Uint16Struct \
  test.conformance/Uint32Struct \
  test.conformance/Uint64Struct \
  test.conformance/Uint64Uint32Uint16Uint8 \
  test.conformance/Uint8Struct \
  test.conformance/Uint8Uint16Uint32Uint64 \
  test.conformance/UnboundedNonnullableString \
  test.conformance/UnboundedNonnullableVectorOfUint32s \
  test.conformance/UpdatePolicy \
  test.conformance/VectorOfStrings \
  test.conformance/VectorWithLimit \
  test.conformance/VectorWrapper)
