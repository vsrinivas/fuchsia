// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type ErrorCode string

// TODO(fxbug.dev/34770) Organize error codes by encoding / decoding.
// Potentially do a check in the parser that the code is the right type.
const (
	_                            ErrorCode = ""
	StringTooLong                          = "STRING_TOO_LONG"
	StringNotUtf8                          = "STRING_NOT_UTF8"
	NonNullableTypeWithNullValue           = "NON_NULLABLE_TYPE_WITH_NULL_VALUE"
	NonEmptyStringWithNullBody             = "NON_EMPTY_STRING_WITH_NULL_BODY"
	UnionFieldNotSet                       = "UNION_FIELD_NOT_SET"
	StrictUnionUnknownField                = "STRICT_UNION_UNKNOWN_FIELD"
	StrictBitsUnknownBit                   = "STRICT_BITS_UNKNOWN_BIT"
	StrictEnumUnknownValue                 = "STRICT_ENUM_UNKNOWN_VALUE"
	ExceededMaxOutOfLineDepth              = "EXCEEDED_MAX_OUT_OF_LINE_DEPTH"
	InvalidPresenceIndicator               = "INVALID_PRESENCE_INDICATOR"
	InvalidNumBytesInEnvelope              = "INVALID_NUM_BYTES_IN_ENVELOPE"
	InvalidNumHandlesInEnvelope            = "INVALID_NUM_HANDLES_IN_ENVELOPE"
	InvalidPaddingByte                     = "INVALID_PADDING_BYTE"
	ExtraHandles                           = "EXTRA_HANDLES"
	TooFewHandles                          = "TOO_FEW_HANDLES"
	NonResourceUnknownHandles              = "NON_RESOURCE_UNKNOWN_HANDLES"
	MissingRequiredHandleRights            = "MISSING_REQUIRED_HANDLE_RIGHTS"
	IncorrectHandleType                    = "INCORRECT_HANDLE_TYPE"
)

var AllErrorCodes = map[ErrorCode]struct{}{
	StringTooLong:                {},
	StringNotUtf8:                {},
	NonNullableTypeWithNullValue: {},
	NonEmptyStringWithNullBody:   {},
	UnionFieldNotSet:             {},
	StrictUnionUnknownField:      {},
	StrictBitsUnknownBit:         {},
	StrictEnumUnknownValue:       {},
	ExceededMaxOutOfLineDepth:    {},
	InvalidPresenceIndicator:     {},
	InvalidNumBytesInEnvelope:    {},
	InvalidNumHandlesInEnvelope:  {},
	InvalidPaddingByte:           {},
	ExtraHandles:                 {},
	TooFewHandles:                {},
	NonResourceUnknownHandles:    {},
	MissingRequiredHandleRights:  {},
	IncorrectHandleType:          {},
}
