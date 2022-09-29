// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package udp_serde

import "fmt"

type InputBufferNullErr struct{}

func (*InputBufferNullErr) Error() string {
	return "null input buffer"
}

type InputBufferTooSmallErr struct{}

func (*InputBufferTooSmallErr) Error() string {
	return "input buffer too small"
}

type FailedToDecodeErr struct{}

func (*FailedToDecodeErr) Error() string {
	return "failed to decode"
}

type FailedToEncodeErr struct{}

func (*FailedToEncodeErr) Error() string {
	return "failed to encode"
}

type PayloadSizeExceedsMaxAllowedErr struct {
	payloadSize int
	maxAllowed  int
}

func (e *PayloadSizeExceedsMaxAllowedErr) Error() string {
	return fmt.Sprintf("payload size (%d) exceeds max allowed (%d)", e.payloadSize, e.maxAllowed)
}

func (e *PayloadSizeExceedsMaxAllowedErr) Is(other error) bool {
	if other, ok := other.(*PayloadSizeExceedsMaxAllowedErr); ok {
		if e == nil {
			return other == nil
		}
		return *e == *other
	}
	return false
}

type UnspecifiedDecodingFailure struct{}

func (*UnspecifiedDecodingFailure) Error() string {
	return "unspecified decoding failure"
}
