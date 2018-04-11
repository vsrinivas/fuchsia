// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"reflect"
	"strconv"
)

// ValidationError represents an error produced in validating a FIDL structure.
type ValidationError interface {
	error

	// Code returns the underlying ErrorCode value, which all ValidationErrors
	// must have.
	Code() ErrorCode
}

// ErrorCode represents a set of machine-readable error codes each ValidationError
// has.
type ErrorCode uint32

const (
	ErrUnknownOrdinal ErrorCode = iota
	ErrInvalidInlineType
	ErrInvalidPointerType
	ErrVectorTooLong
	ErrStringTooLong
	ErrUnexpectedOrdinal
	ErrUnexpectedTxid
	ErrUnexpectedNullHandle
	ErrUnexpectedNullRef
	ErrInvalidBoolValue
	ErrNotEnoughHandles
	ErrBadHandleEncoding
	ErrBadRefEncoding
	ErrMessageTooSmall
	ErrStructIsNotPayload
	ErrInvalidUnionTag
)

// Error implements error for ErrorCode
func (e ErrorCode) Error() string {
	switch e {
	case ErrUnknownOrdinal:
		return "unknown ordinal"
	case ErrInvalidInlineType:
		return "invalid inline type"
	case ErrInvalidPointerType:
		return "invalid pointer type"
	case ErrVectorTooLong:
		return "vector exceeds maximum size"
	case ErrStringTooLong:
		return "string exceeds maximum size"
	case ErrUnexpectedOrdinal:
		return "unexpected ordinal"
	case ErrUnexpectedTxid:
		return "unexpected txid"
	case ErrUnexpectedNullHandle:
		return "unexpected null handle"
	case ErrUnexpectedNullRef:
		return "unexpected null reference"
	case ErrInvalidBoolValue:
		return "unexpected boolean value"
	case ErrNotEnoughHandles:
		return "not enough handles"
	case ErrBadHandleEncoding:
		return "bad encoding for handle"
	case ErrBadRefEncoding:
		return "bad encoding for ref"
	case ErrMessageTooSmall:
		return "message too small to have FIDL header"
	case ErrStructIsNotPayload:
		return "golang struct type must implement Payload"
	default:
		return "unknown error code"
	}
}

// Code implements the ValidationError interface.
func (e ErrorCode) Code() ErrorCode {
	return e
}

// stringer is an interface for types for which a string representation
// may be derived.
type stringer interface {
	String() string
}

// toString generates the string representation for a limited set of types.
func toString(value interface{}) string {
	if e, ok := value.(error); ok {
		return e.Error()
	}
	if s, ok := value.(stringer); ok {
		return s.String()
	}
	t := reflect.TypeOf(value)
	switch t.Kind() {
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return strconv.FormatInt(reflect.ValueOf(value).Int(), 10)
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		return strconv.FormatUint(reflect.ValueOf(value).Uint(), 10)
	case reflect.String:
		return value.(string)
	default:
		return "##BADVALUE##"
	}
}

// valueError represents an error that refers to a single value.
type valueError struct {
	ErrorCode
	value interface{}
}

func newValueError(code ErrorCode, value interface{}) valueError {
	return valueError{
		ErrorCode: code,
		value: value,
	}
}

func (e valueError) Error() string {
	return e.ErrorCode.Error() + ": " + toString(e.value)
}

// expectError represents an error that refers to the expectation of a
// certain value, and displays a comparison between the actual value and
// the expected value.
type expectError struct {
	ErrorCode
	expect interface{}
	actual interface{}
}

func newExpectError(code ErrorCode, expect, actual interface{}) expectError {
	return expectError{
		ErrorCode: code,
		expect: expect,
		actual: actual,
	}
}

func (e expectError) Error() string {
	return e.ErrorCode.Error() + ": expected " + toString(e.expect) + ", got " + toString(e.actual)
}
