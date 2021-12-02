// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

// This file implements a Go equivalent of:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-engine/context_api.cc

package trace

// #include <lib/trace-engine/types.h>
// #include <zircon/types.h>
import "C"

// currentProcessKoid is assigned once by init() for efficiency.
var currentProcessKoid uint64

func GetCurrentProcessKoid() uint64 {
	return currentProcessKoid
}

type stringEntryFlags uint32

const (
	allocIndexAttempted stringEntryFlags = 1 << iota
	allocIndexSucceeded
	categoryChecked
	categoryEnabled
)

type stringEntry struct {
	stringLiteral string
	index         uint64
	flags         stringEntryFlags
}

type threadEntry struct {
	threadKoid uint64
	index      uint64
}

// Fuchsia trace format.
// The binary format of trace records are described in
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/reference/tracing/trace-format.md

type ticksT uint64

type CounterID uint64
type AsyncID uint64
type FlowID uint64

const (
	wordSize uint64 = 8 // bytes

	wordSizeShift = 3 // bits
)

func roundUpToWords(nbytes uint64) uint64 {
	return nbytes + ((wordSize - (nbytes % wordSize)) % wordSize)
}

type payload struct {
	ptr    *[MaxBufferSize]byte
	offset uint64
}

func (c *context) newPayload(isDurable bool, nbytes uint64) (payload, bool) {
	ptr, offset, ok := func() (*[MaxBufferSize]byte, uint64, bool) {
		if isDurable && c.usingDurableBuffer() {
			return c.allocDurableRecord(nbytes)
		} else {
			return c.allocRecord(nbytes)
		}
	}()
	if !ok {
		return payload{}, false
	}
	return payload{
		ptr:    ptr,
		offset: offset,
	}, true
}

func (p *payload) writeUint64(value uint64) {
	nativeEndian.PutUint64((*p.ptr)[p.offset:], value)
	p.offset += wordSize
}

// Record Header.

type recordType int

const (
	metaDataT       recordType = 0
	initializationT            = 1
	stringT                    = 2
	threadT                    = 3
	eventT                     = 4
	blobT                      = 5
	kernelObjectT              = 6
	contextSwitchT             = 7
	logT                       = 8
	// LargeRecord uses a 32-bit size field.
	largeRecordT = 15
)

const (
	recordHeaderSize = wordSize

	maxRecordSizeWords = 0xfff
	maxRecordSizeBytes = maxRecordSizeWords << wordSizeShift

	recordHeaderSizeShift = 4
)

func makeRecordHeader(typ recordType, nbytes uint64) uint64 {
	// recordSize is i
	return uint64(typ) | (nbytes>>wordSizeShift)<<recordHeaderSizeShift
}

// Initialization record.

const initializationRecordSize = recordHeaderSize + wordSize

func (c *context) writeInitializationRecord(ticksPerSecond uint64) {
	p, ok := c.newPayload(true, initializationRecordSize)
	if ok {
		p.writeUint64(makeRecordHeader(initializationT, initializationRecordSize))
		p.writeUint64(ticksPerSecond)
	}
}

// StringRef.

type stringRef struct {
	encodedValue uint64
	inlineString []byte
}

const (
	encodedStringRefEmpty      uint64 = 0
	encodedStringRefInlineFlag        = 0x8000
	encodedStringRefLengthMask        = 0x7fff
	encodedStringRefMaxLength         = 32000
	encodedStringRefMinIndex          = 1
	encodedStringRefMaxIndex          = 0x7fff
)
const (
	encodedThreadRefInline   uint64 = 0
	encodedThreadRefMinIndex        = 1
	encodedThreadRefMaxIndex        = 0xff
)
