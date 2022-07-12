// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package udp_serde

import "fmt"

type InputBufferNullErr struct{}

func (e InputBufferNullErr) Error() string {
	return "null input buffer"
}

type InputBufferTooSmallErr struct{}

func (e InputBufferTooSmallErr) Error() string {
	return "input buffer too small"
}

type NonZeroPreludeErr struct{}

func (e NonZeroPreludeErr) Error() string {
	return "non zero prelude"
}

type FailedToDecodeErr struct{}

func (e FailedToDecodeErr) Error() string {
	return "failed to decode"
}

type FailedToEncodeErr struct{}

func (e FailedToEncodeErr) Error() string {
	return "failed to encode"
}

type PayloadSizeExceedsMaxAllowedErr struct {
	payloadSize int
	maxAllowed  int
}

func (e PayloadSizeExceedsMaxAllowedErr) Error() string {
	return fmt.Sprintf("payload size (%d) exceeds max allowed (%d)", e.payloadSize, e.maxAllowed)
}
