// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

package trace

import (
	"testing"
	"unsafe"
)

const (
	payloadTestBufferSize = 4096
	sentinel              = uint64(0xdeadbeefdeadbeef)
)

func getOneshotBufferStart(vaddr uintptr) *[payloadTestBufferSize]byte {
	return (*[payloadTestBufferSize]byte)(unsafe.Pointer(vaddr + uintptr(BufferHeaderSize)))
}

func getTestContext(tb testing.TB) (*context, *[payloadTestBufferSize]byte) {
	tb.Helper()
	testBufferSize := uint64(payloadTestBufferSize)
	vaddr := getTestBuffer(tb, testBufferSize)
	c := newContext(vaddr, testBufferSize, Oneshot, &testSession{})
	c.clearEntireBuffer()
	return c, getOneshotBufferStart(vaddr)
}

func writeSentinel(t *testing.T, c *context) {
	t.Helper()
	p, ok := c.newPayload(false, wordSize)
	if !ok {
		t.Fatal("newPayload failed")
	}
	p.writeUint64(sentinel)
}

func compareBuffer(t *testing.T, c *context, b *[payloadTestBufferSize]byte, expected []uint64) {
	t.Helper()
	writeSentinel(t, c)
	for i, want := range append(expected, sentinel) {
		if got := nativeEndian.Uint64(b[i*8 : (i+1)*8]); got != want {
			t.Errorf("buffer[%d:%d]: got %#x, want %#x", i*8, (i+1)*8, got, want)
		}
	}
}

func TestPayloadWriteUint64(t *testing.T) {
	value := uint64(0x1234567890abcdef)

	c, b := getTestContext(t)
	p, ok := c.newPayload(false, wordSize)
	if !ok {
		t.Fatal("newPayload failed")
	}
	p.writeUint64(value)
	compareBuffer(t, c, b, []uint64{value})
}

func TestInitializationRecord(t *testing.T) {
	testCases := []struct {
		testName string
		tps      uint64
		buffer   []uint64
	}{
		{
			"100000",
			100000,
			[]uint64{
				uint64(initializationT) | 2<<recordHeaderSizeShift,
				uint64(100000),
			},
		},
		{
			"0x1234567890abcdef",
			0x1234567890abcdef,
			[]uint64{
				uint64(initializationT) | 2<<recordHeaderSizeShift,
				uint64(0x1234567890abcdef),
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			c.writeInitializationRecord(tc.tps)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}
