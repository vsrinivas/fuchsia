// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bitops

import "testing"

func TestLE16(t *testing.T) {
	var buf [2]byte
	input := uint16(0xAABB)
	PutLE16(buf[:], input)
	if buf[0] != 0xBB {
		t.Fatalf("Unexpected value at buf[0]: %x", buf[0])
	} else if buf[1] != 0xAA {
		t.Fatalf("Unexpected value at buf[1]: %x", buf[1])
	}

	output := GetLE16(buf[:])
	if output != input {
		t.Fatalf("Output of GetLE16 did not match input: %x", output)
	}
}

func TestLE32(t *testing.T) {
	var buf [4]byte
	input := uint32(0xAABBCCDD)
	PutLE32(buf[:], input)
	if buf[0] != 0xDD {
		t.Fatalf("Unexpected value at buf[0]: %x", buf[0])
	} else if buf[1] != 0xCC {
		t.Fatalf("Unexpected value at buf[1]: %x", buf[1])
	} else if buf[2] != 0xBB {
		t.Fatalf("Unexpected value at buf[2]: %x", buf[2])
	} else if buf[3] != 0xAA {
		t.Fatalf("Unexpected value at buf[3]: %x", buf[3])
	}

	output := GetLE32(buf[:])
	if output != input {
		t.Fatalf("Output of GetLE16 did not match input: %x", output)
	}
}
