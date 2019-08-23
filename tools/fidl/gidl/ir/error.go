// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type ErrorCode string

// TODO(34770) Organize error codes by encoding / decoding.
// Potentially do a check in the parser that the code is the right type.
const (
	_                           ErrorCode = ""
	StringTooLong                         = "STRING_TOO_LONG"
	NullEmptyStringWithNullBody           = "NON_EMPTY_STRING_WITH_NULL_BODY"
	StrictXUnionFieldNotSet               = "STRICT_XUNION_FIELD_NOT_SET"
	StrictXUnionUnknownField              = "STRICT_XUNION_UNKNOWN_FIELD"
)

var AllErrorCodes = map[ErrorCode]bool{
	StringTooLong:               true,
	NullEmptyStringWithNullBody: true,
	StrictXUnionFieldNotSet:     true,
	StrictXUnionUnknownField:    true,
}
