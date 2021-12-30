// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

// This file implements a Go equivalent of:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-engine/context.cc

package trace

import (
	"fmt"
	"sync/atomic"
	"unsafe"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
)

// #include <lib/trace-engine/buffer_internal.h>
// #include <lib/trace-engine/types.h>
import "C"

// Please read the comment in
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-engine/include/lib/trace-engine/buffer_internal.h
// for the buffer layout and the buffering modes.

type BufferingMode uint8

const (
	Oneshot   BufferingMode = C.TRACE_BUFFERING_MODE_ONESHOT
	Circular                = C.TRACE_BUFFERING_MODE_CIRCULAR
	Streaming               = C.TRACE_BUFFERING_MODE_STREAMING
)

type Handler interface {
	IsCategoryEnabled(category string) bool
	TraceStarted()
	TraceStopped()
	TraceTerminated()
	NotifyBufferFull(wrapperCount uint32, offset uint64)
	SendAlert()
}

type StartMode uint8

const (
	ClearEntireBuffer StartMode = iota + 1
	ClearNonDurableBuffer
	RetainBuffer
)

type BufferHeader C.struct_trace_buffer_header

func (b *BufferHeader) Magic() uint64 {
	var magic C.ulong = b.magic
	return uint64(magic)
}

func (b *BufferHeader) Version() uint16 {
	var version C.ushort = b.version
	return uint16(version)
}

func (b *BufferHeader) DurableDataEnd() uintptr {
	var durableDataEnd C.ulong = b.durable_data_end
	return uintptr(durableDataEnd)
}

func (b *BufferHeader) setDurableDataEnd(e uintptr) {
	b.durable_data_end = C.ulong(e)
}

func (b *BufferHeader) RollingDataEnd() (uintptr, uintptr) {
	var rollingDataEnd0, rollingDataEnd1 C.ulong = b.rolling_data_end[0], b.rolling_data_end[1]
	return uintptr(rollingDataEnd0), uintptr(rollingDataEnd1)
}

func (b *BufferHeader) setRollingDataEnd(e0 uintptr, e1 uintptr) {
	b.rolling_data_end[0] = C.ulong(e0)
	b.rolling_data_end[1] = C.ulong(e1)
}

func (b *BufferHeader) NumRecordsDropped() uint64 {
	var numRecordDropped C.ulong = b.num_records_dropped
	return uint64(numRecordDropped)
}

func (b *BufferHeader) setNumRecordsDropped(n uint64) {
	b.num_records_dropped = C.ulong(n)
}

const (
	BufferHeaderSize  = uint64(unsafe.Sizeof(BufferHeader{}))
	BufferHeaderMagic = uint64(C.TRACE_BUFFER_HEADER_MAGIC)
	BufferHeaderV0    = uint16(C.TRACE_BUFFER_HEADER_V0)

	// DurableBufeer and RollingBuffer should never exceed this
	// size.
	MaxBufferSize = 1 << RollingBufferSizeBits
)

func createBufferHeader(mode BufferingMode, totalSize uint64, durableBufferSize uint64, rollingBufferSize uint64) BufferHeader {
	return BufferHeader{
		magic:               C.TRACE_BUFFER_HEADER_MAGIC,
		version:             C.TRACE_BUFFER_HEADER_V0,
		buffering_mode:      C.uchar(mode),
		total_size:          C.ulong(totalSize),
		durable_buffer_size: C.ulong(durableBufferSize),
		rolling_buffer_size: C.ulong(rollingBufferSize),
	}
}

// We took these values from in zircon/system/ulib/trace-engine/context_impl.h.
// But it doesn't mean they have to match. They are not part of the protocol.
const (
	// The maximum rolling buffer size in bits.
	RollingBufferSizeBits = 32

	// The number of usable bits in the buffer pointer.
	// This is several bits more than the maximum buffer size to
	// allow a buffer pointer to grow without overflow while
	// TraceManager is saving a buffer in streaming mode.
	UsableBufferOffsetBits = RollingBufferSizeBits + 8

	// The number of bits used to record the buffer pointer.
	// This includes one more bit to support overflow in offset calcs.
	BufferOffsetBits = UsableBufferOffsetBits + 1

	// The number of bits in the wrapped counter.
	WrappedCounterBits  = 20
	WrappedCounterShift = 64 - WrappedCounterBits
)

type offsetPlusCounter uintptr

func makeOffsetPlusCounter(bufferOffset uint64, wrappedCount uint32) offsetPlusCounter {
	return offsetPlusCounter(bufferOffset | uint64(wrappedCount<<WrappedCounterShift))
}

func (opc offsetPlusCounter) BufferOffset() uint64 {
	return uint64(opc & ((1 << BufferOffsetBits) - 1))
}

func (opc offsetPlusCounter) WrappedCount() uint32 {
	return uint32(opc >> WrappedCounterShift)
}

func (opc offsetPlusCounter) BufferNumber() int {
	return int(opc.WrappedCount() & 1)
}

type context struct {
	bufferStart uintptr
	bufferSize  uint64
	mode        BufferingMode
	handler     Handler

	durableBufferStart    *[MaxBufferSize]byte
	durableBufferSize     uint64
	durableBufferCurrent  uint64
	durableBufferFullMark uint64

	rollingBufferStart    [2]*[MaxBufferSize]byte
	rollingBufferSize     uint64
	rollingBufferCurrent  offsetPlusCounter
	rollingBufferFullMark [2]uint64

	numRecordsDropped                  uint64
	numRecordsDroppedAfterBufferSwitch uint64

	header *BufferHeader

	mu struct {
		sync.Mutex
		nextThreadIndex uint64
		threadTable     map[uint64]*threadEntry
		nextStringIndex uint64
		stringTable     map[string]*stringEntry
	}
}

func (c *context) setDurableBufferStart(start uintptr) {
	c.durableBufferStart = (*[MaxBufferSize]byte)(unsafe.Pointer(start))
}

func (c *context) clearDurableBufferStart() {
	c.durableBufferStart = nil
}

func (c *context) setDurableBufferSize(size uint64) {
	c.durableBufferSize = size
}

func (c *context) setDurableBufferCurrent(current uint64) {
	c.durableBufferCurrent = current
}

func (c *context) setDurableBufferFullMark(mark uint64) {
	c.durableBufferFullMark = mark
}

func (c *context) setRollingBufferStart(index int, start uintptr) {
	c.rollingBufferStart[index] = (*[MaxBufferSize]byte)(unsafe.Pointer(start))
}

func (c *context) clearRollingBufferStart(index int) {
	c.rollingBufferStart[index] = nil
}

func (c *context) setRollingBufferSize(size uint64) {
	c.rollingBufferSize = size
}

func (c *context) setRollingBufferCurrent(current offsetPlusCounter) {
	c.rollingBufferCurrent = current
}

func (c *context) setRollingBufferFullMark(index int, mark uint64) {
	c.rollingBufferFullMark[index] = mark
}

func (c *context) setHeader(start uintptr) {
	c.header = (*BufferHeader)(unsafe.Pointer(start))
}

func newContext(start uintptr, size uint64, mode BufferingMode, handler Handler) *context {
	if size%4096 != 0 {
		panic(fmt.Sprintf("size is not a multiple of 4096 (%d)", size))
	}
	c := &context{
		bufferStart: start,
		bufferSize:  size,
		mode:        mode,
		handler:     handler,
	}
	c.setHeader(c.bufferStart)
	c.mu.nextThreadIndex = encodedThreadRefMinIndex
	c.mu.threadTable = make(map[uint64]*threadEntry)
	c.mu.nextStringIndex = encodedStringRefMinIndex
	c.mu.stringTable = make(map[string]*stringEntry)
	c.initBufferSizes()
	c.resetBufferPointers()
	return c
}

func (c *context) allocThreadIndexLocked() (uint64, bool) {
	index := c.mu.nextThreadIndex
	c.mu.nextThreadIndex++
	if index > encodedThreadRefMaxIndex {
		return encodedThreadRefMaxIndex + 1, false
	}
	return index, true
}

func (c *context) allocStringIndexLocked() (uint64, bool) {
	index := c.mu.nextStringIndex
	c.mu.nextStringIndex++
	if index > encodedStringRefMaxIndex+1 {
		return encodedStringRefMaxIndex + 1, false
	}
	return index, true
}

func (c *context) initBufferSizes() {
	switch mode := c.mode; mode {
	case Oneshot:
		c.setDurableBufferSize(0)
		c.clearDurableBufferStart()
		c.setRollingBufferSize(c.bufferSize - BufferHeaderSize)
		c.setRollingBufferStart(0, c.bufferStart+uintptr(BufferHeaderSize))
		c.clearRollingBufferStart(1)
	case Circular:
		// TODO(https://fxbug.dev/78263): Support circular mode.
		panic("circular mode is not supported")
	case Streaming:
		// TODO(https://fxbug.dev/78265): Support streaming mode.
		panic("streaming mode is not supported")
	default:
		panic(fmt.Sprintf("unknown mode (%d)", mode))
	}
}

func (c *context) resetDurableBufferPointers() {
	c.setDurableBufferCurrent(0)
	c.setDurableBufferFullMark(0)
}

func (c *context) resetRollingBufferPointers() {
	c.setRollingBufferCurrent(0)
	c.setRollingBufferFullMark(0, 0)
	c.setRollingBufferFullMark(1, 0)
}

func (c *context) resetBufferPointers() {
	c.resetDurableBufferPointers()
	c.resetRollingBufferPointers()
}

func (c *context) initBufferHeader() {
	*c.header = createBufferHeader(c.mode, c.bufferSize, c.durableBufferSize, c.rollingBufferSize)
}

func (c *context) clearEntireBuffer() {
	c.resetBufferPointers()
	c.initBufferHeader()
}

func (c *context) clearRollingBuffers() {
	c.resetRollingBufferPointers()
}

func (c *context) UpdateBufferHeaderAfterStopped() {
	// If the buffer filled, then the current pointer is "snapped"
	// to the end. Therefore in that case we need to use the
	// buffer_full_mark.
	durableLastOffset := c.durableBufferCurrent
	durableBufferFullMark := c.durableBufferFullMark
	if durableBufferFullMark != 0 {
		durableLastOffset = durableBufferFullMark
	}
	c.header.durable_data_end = C.ulong(durableLastOffset)

	lastOffset := c.rollingBufferCurrent.BufferOffset()
	wrappedCount := c.rollingBufferCurrent.WrappedCount()
	c.header.wrapped_count = C.uint(wrappedCount)
	bufferNumber := c.rollingBufferCurrent.BufferNumber()
	bufferFullMark := c.rollingBufferFullMark[bufferNumber]
	if bufferFullMark != 0 {
		lastOffset = bufferFullMark
	}
	c.header.rolling_data_end[bufferNumber] = C.ulong(lastOffset)

	c.header.num_records_dropped = C.ulong(c.numRecordsDropped)
}

func (c *context) usingDurableBuffer() bool {
	return c.mode != Oneshot
}

func (c *context) allocDurableRecord(nbytes uint64) (*[MaxBufferSize]byte, uint64, bool) {
	// TODO(https://fxbug.dev/78263): Support circular mode.
	return nil, 0, false
}

func (c *context) allocRecord(nbytes uint64) (*[MaxBufferSize]byte, uint64, bool) {
	switch mode := c.mode; mode {
	case Oneshot:
		opc := offsetPlusCounter(atomic.AddUintptr((*uintptr)(&c.rollingBufferCurrent), uintptr(nbytes)))
		bufferNumber := opc.BufferNumber()
		bufferOffset := opc.BufferOffset()
		if bufferOffset > c.rollingBufferSize {
			// Buffer is full.
			c.markOneshotBufferFull(bufferOffset - nbytes)
			return nil, 0, false
		}
		return c.rollingBufferStart[bufferNumber], bufferOffset - nbytes, true
	case Circular:
		// TODO(https://fxbug.dev/78263): Support circular mode.
		panic("circular mode is not supported")
	case Streaming:
		// TODO(https://fxbug.dev/78265): Support streaming mode.
		panic("streaming mode is not supported")
	default:
		panic(fmt.Sprintf("unknown mode (%d)", mode))
	}
	return nil, 0, false
}

func (c *context) markOneshotBufferFull(lastOffset uint64) {
	c.snapToEnd(0)

	// Mark the end point if not already marked.
	if atomic.CompareAndSwapUint64(&c.rollingBufferFullMark[0], 0, lastOffset) {
		c.header.rolling_data_end[0] = C.ulong(lastOffset)
	}
	c.markRecordDropped()
}

func (c *context) snapToEnd(wrappedCount uint32) {
	// Snap to the endpoint for simplicity. Several threads could
	// all hit buffer-full with each one continually incrementing
	// the offset.
	fullOffsetPlusCounter := makeOffsetPlusCounter(c.rollingBufferSize, wrappedCount)
	atomic.StoreUintptr((*uintptr)(&c.rollingBufferCurrent), uintptr(fullOffsetPlusCounter))
}

func (c *context) markRecordDropped() {
	atomic.AddUint64(&c.numRecordsDropped, 1)
}
