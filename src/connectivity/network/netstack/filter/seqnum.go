// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package filter

type seqnum uint32

func (a seqnum) LessThan(b seqnum) bool {
	return int32(a-b) < 0
}

func (a seqnum) LessThanEq(b seqnum) bool {
	return int32(a-b) <= 0
}

func (a seqnum) GreaterThan(b seqnum) bool {
	return int(a-b) > 0
}

func (a seqnum) GreaterThanEq(b seqnum) bool {
	return int(a-b) >= 0
}

func (a seqnum) Add(b uint32) seqnum {
	return a + seqnum(b)
}

func (a seqnum) Sub(b uint32) seqnum {
	return a - seqnum(b)
}
