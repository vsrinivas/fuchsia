// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type ErrorCode string

// TODO(34770) Organize error codes by encoding / decoding.
// Potentially do a check in the parser that the code is the right type.
const (
	_                          ErrorCode = ""
	StringTooLong                        = "STRING_TOO_LONG"
	NonEmptyStringWithNullBody           = "NON_EMPTY_STRING_WITH_NULL_BODY"
	StrictUnionFieldNotSet               = "STRICT_UNION_FIELD_NOT_SET"
	StrictUnionUnknownField              = "STRICT_UNION_UNKNOWN_FIELD"
)

var AllErrorCodes = map[ErrorCode]bool{
	StringTooLong:              true,
	NonEmptyStringWithNullBody: true,
	StrictUnionFieldNotSet:     true,
	StrictUnionUnknownField:    true,
}
