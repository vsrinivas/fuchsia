// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type ErrorCode string

const (
	CountExceedsLimit                  ErrorCode = "COUNT_EXCEEDS_LIMIT"
	EnvelopeBytesExceedMessageLength   ErrorCode = "ENVELOPE_BYTES_EXCEED_MESSAGE_LENGTH"
	EnvelopeHandlesExceedMessageLength ErrorCode = "ENVELOPE_HANDLES_EXCEED_MESSAGE_LENGTH"
	ExceededMaxOutOfLineDepth          ErrorCode = "EXCEEDED_MAX_OUT_OF_LINE_DEPTH"
	IncorrectHandleType                ErrorCode = "INCORRECT_HANDLE_TYPE"
	InvalidBoolean                     ErrorCode = "INVALID_BOOLEAN"
	InvalidEmptyStruct                 ErrorCode = "INVALID_EMPTY_STRUCT"
	InvalidInlineBitInEnvelope         ErrorCode = "INVALID_INLINE_BIT_IN_ENVELOPE"
	InvalidNumBytesInEnvelope          ErrorCode = "INVALID_NUM_BYTES_IN_ENVELOPE"
	InvalidNumHandlesInEnvelope        ErrorCode = "INVALID_NUM_HANDLES_IN_ENVELOPE"
	InvalidPaddingByte                 ErrorCode = "INVALID_PADDING_BYTE"
	InvalidPresenceIndicator           ErrorCode = "INVALID_PRESENCE_INDICATOR"
	MissingRequiredHandleRights        ErrorCode = "MISSING_REQUIRED_HANDLE_RIGHTS"
	NonEmptyStringWithNullBody         ErrorCode = "NON_EMPTY_STRING_WITH_NULL_BODY"
	NonEmptyVectorWithNullBody         ErrorCode = "NON_EMPTY_VECTOR_WITH_NULL_BODY"
	NonNullableTypeWithNullValue       ErrorCode = "NON_NULLABLE_TYPE_WITH_NULL_VALUE"
	NonResourceUnknownHandles          ErrorCode = "NON_RESOURCE_UNKNOWN_HANDLES"
	StrictBitsUnknownBit               ErrorCode = "STRICT_BITS_UNKNOWN_BIT"
	StrictEnumUnknownValue             ErrorCode = "STRICT_ENUM_UNKNOWN_VALUE"
	StrictUnionUnknownField            ErrorCode = "STRICT_UNION_UNKNOWN_FIELD"
	StringNotUtf8                      ErrorCode = "STRING_NOT_UTF8"
	StringTooLong                      ErrorCode = "STRING_TOO_LONG"
	TooFewBytes                        ErrorCode = "TOO_FEW_BYTES"
	TooFewBytesInPrimaryObject         ErrorCode = "TOO_FEW_BYTES_IN_PRIMARY_OBJECT"
	TooFewHandles                      ErrorCode = "TOO_FEW_HANDLES"
	TooManyBytesInMessage              ErrorCode = "TOO_MANY_BYTES_IN_MESSAGE"
	TooManyHandlesInMessage            ErrorCode = "TOO_MANY_HANDLES_IN_MESSAGE"
	UnexpectedOrdinal                  ErrorCode = "UNEXPECTED_ORDINAL"
	UnionFieldNotSet                   ErrorCode = "UNION_FIELD_NOT_SET"
)

var AllErrorCodes = map[ErrorCode]struct{}{
	CountExceedsLimit:                  {},
	EnvelopeBytesExceedMessageLength:   {},
	EnvelopeHandlesExceedMessageLength: {},
	ExceededMaxOutOfLineDepth:          {},
	IncorrectHandleType:                {},
	InvalidBoolean:                     {},
	InvalidEmptyStruct:                 {},
	InvalidInlineBitInEnvelope:         {},
	InvalidNumBytesInEnvelope:          {},
	InvalidNumHandlesInEnvelope:        {},
	InvalidPaddingByte:                 {},
	InvalidPresenceIndicator:           {},
	MissingRequiredHandleRights:        {},
	NonEmptyStringWithNullBody:         {},
	NonEmptyVectorWithNullBody:         {},
	NonNullableTypeWithNullValue:       {},
	NonResourceUnknownHandles:          {},
	StrictBitsUnknownBit:               {},
	StrictEnumUnknownValue:             {},
	StrictUnionUnknownField:            {},
	StringNotUtf8:                      {},
	StringTooLong:                      {},
	TooFewBytes:                        {},
	TooFewBytesInPrimaryObject:         {},
	TooFewHandles:                      {},
	TooManyBytesInMessage:              {},
	TooManyHandlesInMessage:            {},
	UnexpectedOrdinal:                  {},
	UnionFieldNotSet:                   {},
}
