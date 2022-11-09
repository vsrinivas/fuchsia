// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/wire_coding_traits.h>

namespace fidl::internal {

// Define errors in a .cc file to avoid duplicate definitions in each .cc file that
// includes a .h definition.
const char* const kCodingErrorNullValue = "null input pointer";
const char* const kCodingErrorNullIovecBuffer = "null iovec buffer";
const char* const kCodingErrorNullByteBuffer = "null byte buffer";
const char* const kCodingErrorNullHandleBufferButNonzeroCount =
    "null handle buffer but handle count is non-zero";
const char* const kCodingErrorInvalidBoolean = "invalid boolean value";
const char* const kCodingErrorVectorLimitExceeded = "vector limit exceeded";
const char* const kCodingErrorNullDataReceivedForNonNullableVector =
    "null data received for non-nullable vector";
const char* const kCodingErrorNullVectorMustHaveSizeZero = "null vector must have a size of zero";
const char* const kCodingErrorStringLimitExceeded = "string limit exceeded";
const char* const kCodingErrorNullDataReceivedForNonNullableString =
    "null data received for non-nullable string";
const char* const kCodingErrorNullStringMustHaveSizeZero = "null string must have a size of zero";
const char* const kCodingErrorStringNotValidUtf8 = "string is not valid utf-8";
const char* const kCodingErrorNullDataReceivedForTable = "null vector data received for table";
const char* const kCodingErrorInvalidNumBytesSpecifiedInEnvelope =
    "invalid num bytes specified in envelope";
const char* const kCodingErrorInvalidNumHandlesSpecifiedInEnvelope =
    "invalid num handles specified in envelope";
const char* const kCodingErrorNonEmptyByteCountInNullEnvelope =
    "invalid non-empty byte count in null envelope";
const char* const kCodingErrorNonEmptyHandleCountInNullEnvelope =
    "invalid non-empty handle count in null envelope";
const char* const kCodingErrorInvalidInlineBit = "invalid inline bit in envelope";
const char* const kCodingErrorUnknownBitSetInBitsValue = "unknown bit set in bits value";
const char* const kCodingErrorUnknownEnumValue = "not a valid enum value";
const char* const kCodingErrorUnknownUnionTag = "unknown union tag";
const char* const kCodingErrorInvalidUnionTag = "non-nullable union is absent";
const char* const kCodingErrorInvalidPaddingBytes = "invalid padding bytes";
const char* const kCodingErrorRecursionDepthExceeded = "recursion depth exceeded";
const char* const kCodingErrorInvalidPresenceIndicator = "invalid presence indicator";
const char* const kCodingErrorNotAllBytesConsumed = "not all bytes consumed";
const char* const kCodingErrorNotAllHandlesConsumed = "not all handles consumed";
const char* const kCodingErrorAllocationSizeExceeds32Bits = "allocation size exceeds 32-bits";
const char* const kCodingErrorBackingBufferSizeExceeded = "backing buffer size exceeded";
const char* const kCodingErrorTooManyHandlesConsumed =
    "more handles consumed than exist in message";
const char* const kCodingErrorAbsentNonNullableHandle = "non-nullable handle is absent";
const char* const kCodingErrorDoesNotSupportV1Envelopes =
    "does not support decoding envelopes in the V1 wire format";
const char* const kCodingErrorInvalidWireFormatMetadata = "invalid wire format metadata";
const char* const kCodingErrorUnsupportedWireFormatVersion = "unsupported wire format version";
const char* const kCodingErrorAtLeastOneIovecIsNeeded = "at least one iovec is needed";
const char* const kCodingErrorTableFrameLargerThanExpected = "table frame larger than expected";
const char* const kCodingErrorInvalidNullEnvelope = "null envelope contained non-null bytes";
const char* const kCodingErrorInvalidHandleInInput = "invalid handle in input";
const char* const kCodingErrorZeroTagButNonZeroEnvelope = "zero tag but non-zero envelope";
const char* const kCodingErrorDataTooShort = "input data is too short";

}  // namespace fidl::internal
