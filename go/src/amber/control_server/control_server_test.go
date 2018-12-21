// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package control_server

import (
	"fmt"
	"testing"
)

func TestChunkBuffer(t *testing.T) {
	testInvalidChunkSize(t)

	buf := []byte("A buffer of 20 bytes")

	chunks := chunkBuffer(buf, 2*len(buf))
	if err := validateBuffers(buf, chunks); err != nil {
		t.Fatalf("error when chunk exceeds message length: %s", err)
	}

	if len(buf)/4*4 != len(buf) {
		t.Fatalf("internal test error, sample byte buffer is not evenly divisible by 2")
	}
	chunks = chunkBuffer(buf, len(buf)/4)
	if err := validateBuffers(buf, chunks); err != nil {
		t.Fatalf("error when message chunks evenly: %s", err)
	}

	chunks = chunkBuffer(buf, len(buf)-1)
	if err := validateBuffers(buf, chunks); err != nil {
		t.Fatalf("error when message does not chunk evenly: %s", err)
	}
}

func testInvalidChunkSize(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("chunkBuffer should have panic, but did not")
		}
	}()
	chunkBuffer([]byte{}, 0)
}

func validateBuffers(orig []byte, chunks [][]byte) error {
	copy := []byte{}

	for _, chunk := range chunks {
		copy = append(copy, chunk...)
	}

	if len(orig) != len(copy) {
		return fmt.Errorf("buffers are of different length")
	}

	for i := range orig {
		if orig[i] != copy[i] {
			return fmt.Errorf("buffers differ at %d, expected %x but got %x", i, orig[i], copy[i])
		}
	}

	return nil
}
