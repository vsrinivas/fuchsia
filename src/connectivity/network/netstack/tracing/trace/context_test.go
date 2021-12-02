// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

package trace

import (
	"testing"
	"unsafe"

	"github.com/google/go-cmp/cmp"
)

const contextTestMaxBufferSize = 4 * 4096

var buffer [contextTestMaxBufferSize]byte

func getTestBuffer(tb testing.TB, size uint64) uintptr {
	tb.Helper()
	if size > contextTestMaxBufferSize {
		tb.Fatalf("Size (%d) is larger than %d", size, contextTestMaxBufferSize)
	}
	if size%4096 != 0 {
		tb.Fatalf("Size (%d) is not a multiple of 4096", size)
	}
	// Fill with a debug pattern.
	for i := uint64(0); i < size; i += 4 {
		buffer[i] = 0xde
		buffer[i+1] = 0xad
		buffer[i+2] = 0xbe
		buffer[i+3] = 0xef
	}
	return uintptr(unsafe.Pointer(&buffer))
}

var _ Handler = (*testSession)(nil)

type testSession struct {
	categories []string
}

func (s *testSession) setCategories(categories []string) {
	s.categories = categories
}

func (s *testSession) IsCategoryEnabled(category string) bool {
	if len(s.categories) == 0 {
		return true
	}
	for _, cat := range s.categories {
		if cat == category {
			return true
		}
	}
	return false
}

func (s *testSession) TraceStarted() {}

func (s *testSession) TraceStopped() {}

func (s *testSession) TraceTerminated() {}

func (s *testSession) NotifyBufferFull(wrapperCount uint32, offset uint64) {}

func (s *testSession) SendAlert() {}

func TestOneshotBufferHeader(t *testing.T) {
	bufferSize := uint64(4096)
	bufferStart := getTestBuffer(t, bufferSize)
	c := newContext(bufferStart, bufferSize, Oneshot, &testSession{})
	c.clearEntireBuffer()
	header := (*BufferHeader)(unsafe.Pointer(bufferStart))
	headerWant := createBufferHeader(Oneshot, bufferSize, 0, bufferSize-BufferHeaderSize)
	if got, want := *header, headerWant; got != want {
		t.Errorf("got *header = %#v, want = %#v", got, want)
	}

	c.newPayload(true, wordSize)
	// Oneshot mode uses RollingBuffer[0] even for a durable record.
	e0, e1 := headerWant.RollingDataEnd()
	headerWant.setRollingDataEnd(e0+uintptr(wordSize), e1)

	c.newPayload(false, wordSize)
	e0, e1 = headerWant.RollingDataEnd()
	headerWant.setRollingDataEnd(e0+uintptr(wordSize), e1)

	c.UpdateBufferHeaderAfterStopped()
	if diff := cmp.Diff(headerWant, *header, cmp.AllowUnexported(BufferHeader{})); diff != "" {
		t.Errorf("header mismatch (-want +got):\n%s", diff)
	}
}

func TestOneshotBufferFull(t *testing.T) {
	bufferSize := uint64(4096)
	bufferStart := getTestBuffer(t, bufferSize)
	c := newContext(bufferStart, bufferSize, Oneshot, &testSession{})
	c.clearEntireBuffer()
	header := (*BufferHeader)(unsafe.Pointer(bufferStart))
	headerWant := createBufferHeader(Oneshot, bufferSize, 0, bufferSize-BufferHeaderSize)
	if got, want := *header, headerWant; got != want {
		t.Errorf("got *header = %#v, want = %#v", got, want)
	}

	recordSize := 2 * wordSize
	numRecords := (bufferSize - BufferHeaderSize) / recordSize
	numRecordsToDrop := uint64(3)

	for i := uint64(0); i < numRecords+numRecordsToDrop; i++ {
		c.newPayload(true, recordSize)
	}

	e0, e1 := headerWant.RollingDataEnd()
	headerWant.setRollingDataEnd(e0+uintptr(recordSize*numRecords), e1)
	headerWant.setNumRecordsDropped(headerWant.NumRecordsDropped() + numRecordsToDrop)

	c.UpdateBufferHeaderAfterStopped()
	if diff := cmp.Diff(headerWant, *header, cmp.AllowUnexported(BufferHeader{})); diff != "" {
		t.Errorf("header mismatch (-want +got):\n%s", diff)
	}
}
