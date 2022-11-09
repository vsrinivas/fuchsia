// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CODING_ERRORS_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CODING_ERRORS_H_

namespace fidl::internal {

// Use extern definitions of errors to avoid a copy for each .cc file including
// this .h file.
extern const char* const kCodingErrorNullValue;
extern const char* const kCodingErrorNullIovecBuffer;
extern const char* const kCodingErrorNullByteBuffer;
extern const char* const kCodingErrorNullHandleBufferButNonzeroCount;
extern const char* const kCodingErrorInvalidBoolean;
extern const char* const kCodingErrorVectorLimitExceeded;
extern const char* const kCodingErrorNullDataReceivedForNonNullableVector;
extern const char* const kCodingErrorNullVectorMustHaveSizeZero;
extern const char* const kCodingErrorStringLimitExceeded;
extern const char* const kCodingErrorNullDataReceivedForNonNullableString;
extern const char* const kCodingErrorNullStringMustHaveSizeZero;
extern const char* const kCodingErrorStringNotValidUtf8;
extern const char* const kCodingErrorNullDataReceivedForTable;
extern const char* const kCodingErrorInvalidNumBytesSpecifiedInEnvelope;
extern const char* const kCodingErrorInvalidNumHandlesSpecifiedInEnvelope;
extern const char* const kCodingErrorNonEmptyByteCountInNullEnvelope;
extern const char* const kCodingErrorNonEmptyHandleCountInNullEnvelope;
extern const char* const kCodingErrorInvalidInlineBit;
extern const char* const kCodingErrorUnknownBitSetInBitsValue;
extern const char* const kCodingErrorUnknownEnumValue;
extern const char* const kCodingErrorUnknownUnionTag;
extern const char* const kCodingErrorInvalidUnionTag;
extern const char* const kCodingErrorInvalidPaddingBytes;
extern const char* const kCodingErrorRecursionDepthExceeded;
extern const char* const kCodingErrorInvalidPresenceIndicator;
extern const char* const kCodingErrorNotAllBytesConsumed;
extern const char* const kCodingErrorNotAllHandlesConsumed;
extern const char* const kCodingErrorAllocationSizeExceeds32Bits;
extern const char* const kCodingErrorBackingBufferSizeExceeded;
extern const char* const kCodingErrorTooManyHandlesConsumed;
extern const char* const kCodingErrorAbsentNonNullableHandle;
extern const char* const kCodingErrorInvalidWireFormatMetadata;
extern const char* const kCodingErrorDoesNotSupportV1Envelopes;
extern const char* const kCodingErrorUnsupportedWireFormatVersion;
extern const char* const kCodingErrorAtLeastOneIovecIsNeeded;
extern const char* const kCodingErrorTableFrameLargerThanExpected;
extern const char* const kCodingErrorInvalidNullEnvelope;
extern const char* const kCodingErrorInvalidHandleInInput;
extern const char* const kCodingErrorZeroTagButNonZeroEnvelope;
extern const char* const kCodingErrorDataTooShort;

}  // namespace fidl::internal

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CODING_ERRORS_H_
