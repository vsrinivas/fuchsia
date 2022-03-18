// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_coding_traits.h>

namespace fidl::internal {

// Define errors in a .cc file to avoid duplicate definitions in each .cc file that
// includes a .h definition.
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
const char* const kCodingErrorNullTableMustHaveSizeZero = "null table must have a size of zero";
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
const char* const kCodingErrorUnknownEnumValue = "unknown enum value";
const char* const kCodingErrorUnknownUnionTag = "unknown union tag";
const char* const kCodingErrorInvalidPaddingBytes = "invalid padding bytes";
const char* const kCodingErrorRecursionDepthExceeded = "recursion depth exceeded";
const char* const kCodingErrorInvalidPresenceIndicator = "invalid presence indicator";
const char* const kCodingErrorNotAllBytesConsumed = "not all bytes consumed";
const char* const kCodingErrorNotAllHandlesConsumed = "not all handles consumed";
const char* const kCodingErrorAllocationSizeExceeds32Bits = "allocation size exceeds 32-bits";
const char* const kCodingErrorOutOfLineObjectExceedsMessageBounds =
    "out of line object exceeds message bounds";
const char* const kCodingErrorTooManyHandlesConsumed =
    "more handles consumed than exist in message";
const char* const kCodingErrorAbsentNonNullableHandle = "non-nullable handle is absent";

}  // namespace fidl::internal
