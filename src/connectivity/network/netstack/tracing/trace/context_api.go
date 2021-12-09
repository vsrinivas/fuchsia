// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

// This file implements a Go equivalent of:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-engine/context_api.cc

package trace

import (
	"fmt"
	"syscall/zx"
	"unsafe"
)

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

var wordWithZeros [wordSize]byte

func (p *payload) writeBytes(value []byte) {
	written := uint64(copy((*p.ptr)[p.offset:], value))
	if l := uint64(len(value)); written != l {
		panic(fmt.Sprintf("partial copy (%d out of %d)", written, l))
	}
	p.offset += written
	if remainder := uint64(written) % wordSize; remainder != 0 {
		padding := wordSize - remainder
		written := uint64(copy((*p.ptr)[p.offset:], wordWithZeros[:padding]))
		if written != padding {
			panic(fmt.Sprintf("partial copy (%d out of %d)", written, padding))
		}
		p.offset += written
	}
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

// Arguments.

const (
	argumentHeaderSize = wordSize

	argumentHeaderSizeShift  = 4
	argumentHeaderNameShift  = 16
	argumentHeaderValueShift = 32
)

func makeArgumentHeader(typ argumentType, nbytes uint64, nameRef *stringRef) uint64 {
	return uint64(typ) | (nbytes>>wordSizeShift)<<argumentHeaderSizeShift | nameRef.encodedValue<<argumentHeaderNameShift
}

type argumentType int

const (
	nullArgT    argumentType = C.TRACE_ARG_NULL
	int32ArgT                = C.TRACE_ARG_INT32
	uint32ArgT               = C.TRACE_ARG_UINT32
	int64ArgT                = C.TRACE_ARG_INT64
	uint64ArgT               = C.TRACE_ARG_UINT64
	doubleArgT               = C.TRACE_ARG_DOUBLE
	stringArgT               = C.TRACE_ARG_STRING
	pointerArgT              = C.TRACE_ARG_POINTER
	koidArgT                 = C.TRACE_ARG_KOID
	boolArgT                 = C.TRACE_ARG_BOOL
)

type argValue interface {
	writeArgumentHeaderAndName(p *payload, nameRef *stringRef)
	encodedSize() uint64
}

type argValueNull struct{}

func (argValueNull) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(nullArgT, nameRef, 0, 0)
}

func (argValueNull) encodedSize() uint64 {
	return 0
}

func makeArgValueNull() argValue {
	return argValueNull{}
}

type argValueInt32 struct {
	v int32
}

func (i32 argValueInt32) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(int32ArgT, nameRef, 0, uint64(i32.v)<<argumentHeaderValueShift)
}

func (i32 argValueInt32) encodedSize() uint64 {
	return 0
}

func makeArgValueInt32(val int32) argValue {
	return argValueInt32{v: val}
}

type argValueUint32 struct {
	v uint32
}

func (u32 argValueUint32) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(uint32ArgT, nameRef, 0, uint64(u32.v)<<argumentHeaderValueShift)
}

func (u32 argValueUint32) encodedSize() uint64 {
	return 0
}

func makeArgValueUint32(val uint32) argValue {
	return argValueUint32{v: val}
}

type argValueInt64 struct {
	v int64
}

func (i64 argValueInt64) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(int64ArgT, nameRef, wordSize, 0)
	p.writeUint64(uint64(i64.v))
}

func (i64 argValueInt64) encodedSize() uint64 {
	return wordSize
}

func makeArgValueInt64(val int64) argValue {
	return argValueInt64{v: val}
}

type argValueUint64 struct {
	v uint64
}

func (u64 argValueUint64) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(uint64ArgT, nameRef, wordSize, 0)
	p.writeUint64(u64.v)
}

func (u64 argValueUint64) encodedSize() uint64 {
	return wordSize
}

func makeArgValueUint64(val uint64) argValue {
	return argValueUint64{v: val}
}

type argValueDouble struct {
	v float64
}

func (d argValueDouble) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(doubleArgT, nameRef, wordSize, 0)
	p.writeUint64(*(*uint64)(unsafe.Pointer(&d.v)))
}

func (d argValueDouble) encodedSize() uint64 {
	return wordSize
}

func makeArgValueDouble(val float64) argValue {
	return argValueDouble{v: val}
}

type argValueString struct {
	v *stringRef
}

func (s argValueString) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(stringArgT, nameRef,
		sizeOfEncodedStringRef(s.v),
		s.v.encodedValue<<argumentHeaderValueShift)
	p.writeStringRef(s.v)
}

func (s argValueString) encodedSize() uint64 {
	return sizeOfEncodedStringRef(s.v)
}

func makeArgValueString(val stringRef) argValue {
	return argValueString{v: &val}
}

type argValuePointer struct {
	v uintptr
}

func (pt argValuePointer) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(pointerArgT, nameRef, wordSize, 0)
	p.writeUint64(uint64(pt.v))
}

func (pt argValuePointer) encodedSize() uint64 {
	return wordSize
}

func makeArgValuePointer(val uintptr) argValue {
	return argValuePointer{v: val}
}

type argValueKoid struct {
	v uint64
}

func (k argValueKoid) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	p.writeArgumentHeaderAndName(koidArgT, nameRef, wordSize, 0)
	p.writeUint64(k.v)
}

func (k argValueKoid) encodedSize() uint64 {
	return wordSize
}

func makeArgValueKoid(val uint64) argValue {
	return argValueKoid{v: val}
}

type argValueBool struct {
	v bool
}

func (b argValueBool) writeArgumentHeaderAndName(p *payload, nameRef *stringRef) {
	var boolValue uint64
	if b.v {
		boolValue = uint64(1)
	}
	p.writeArgumentHeaderAndName(boolArgT, nameRef, 0, boolValue<<argumentHeaderValueShift)
}

func (b argValueBool) encodedSize() uint64 {
	return 0
}

func makeArgValueBool(val bool) argValue {
	return argValueBool{v: val}
}

// Arg is a typed key value pair.
// Many trace record types can optionally include an array of Args to provide
// additional information.
// TODO(https://fxbug.dev/89762): Export APIs to use Arg.
type Arg struct {
	NameRef stringRef
	Value   argValue
}

func sizeOfEncodedArg(arg *Arg) uint64 {
	return argumentHeaderSize + sizeOfEncodedStringRef(&arg.NameRef) +
		arg.Value.encodedSize()
}

func sizeOfEncodedArgs(args []Arg) uint64 {
	total := uint64(0)
	for _, arg := range args {
		total += sizeOfEncodedArg(&arg)
	}
	return total
}

func (p *payload) writeArgumentHeaderAndName(typ argumentType, nameRef *stringRef, contentSize uint64, headerBits uint64) {
	argumentSize := argumentHeaderSize + sizeOfEncodedStringRef(nameRef) + contentSize
	p.writeUint64(makeArgumentHeader(typ, argumentSize, nameRef) | headerBits)
	p.writeStringRef(nameRef)
}

func (p *payload) writeArg(arg *Arg) {
	arg.Value.writeArgumentHeaderAndName(p, &arg.NameRef)
}

func (p *payload) writeArgs(args []Arg) {
	for i := range args {
		p.writeArg(&args[i])
	}
}

// Initialization record.

const initializationRecordSize = recordHeaderSize + wordSize

func (c *context) writeInitializationRecord(ticksPerSecond uint64) {
	p, ok := c.newPayload(true, initializationRecordSize)
	if !ok {
		return
	}
	p.writeUint64(makeRecordHeader(initializationT, initializationRecordSize))
	p.writeUint64(ticksPerSecond)
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

func makeEmptyStringRef() stringRef {
	return stringRef{
		encodedValue: encodedStringRefEmpty,
		inlineString: nil,
	}
}

func makeInlineStringRef(str []byte) stringRef {
	l := uint64(len(str))
	if l > encodedStringRefMaxLength {
		l = encodedStringRefMaxLength
	}
	return stringRef{
		encodedValue: encodedStringRefInlineFlag | l,
		inlineString: str[:l],
	}
}

func makeIndexedStringRef(index uint64) stringRef {
	return stringRef{
		encodedValue: index,
		inlineString: nil,
	}
}

func (r *stringRef) IsInlineStringRef() bool {
	return (r.encodedValue & encodedStringRefInlineFlag) != 0
}

func sizeOfEncodedStringRef(r *stringRef) uint64 {
	if r.IsInlineStringRef() {
		return roundUpToWords(uint64(len(r.inlineString)))
	}
	return 0
}

func (p *payload) writeStringRef(r *stringRef) {
	if r.IsInlineStringRef() {
		p.writeBytes(r.inlineString)
	}
}

func (c *context) checkCategory(category string) bool {
	return c.handler.IsCategoryEnabled(category)
}

// String record.

const (
	stringRecordStringIndexShift  = 16
	stringRecordStringLengthShift = 32
)

func (c *context) writeStringRecord(isDurable bool, index uint64, str []byte) bool {
	l := uint64(len(str))
	if l > encodedStringRefMaxLength {
		l = encodedStringRefMaxLength
	}
	recordSize := recordHeaderSize + roundUpToWords(l)
	p, ok := c.newPayload(isDurable, recordSize)
	if !ok {
		return false
	}
	p.writeUint64(makeRecordHeader(stringT, recordSize) |
		uint64(index<<stringRecordStringIndexShift) |
		uint64(l<<stringRecordStringLengthShift))
	p.writeBytes(str[:l])
	return true
}

func (c *context) cacheStringEntryLocked(s string) *stringEntry {
	if entry, ok := c.mu.stringTable[s]; ok {
		return entry
	}
	entry := &stringEntry{
		stringLiteral: s,
	}
	c.mu.stringTable[s] = entry
	return entry
}

func (c *context) registerStringLocked(s string, checkCategory bool) (stringRef, bool) {
	entry := c.cacheStringEntryLocked(s)
	if checkCategory {
		if entry.flags&categoryChecked == 0 {
			if c.checkCategory(s) {
				entry.flags |= categoryChecked | categoryEnabled
			}
		}
		if entry.flags&categoryEnabled == 0 {
			// If the category is not enabled, return immediately.
			return stringRef{}, false
		}
	}
	if entry.flags&allocIndexAttempted == 0 {
		entry.flags |= allocIndexAttempted
		if index, ok := c.allocStringIndexLocked(); ok && c.writeStringRecord(true, index, []byte(s)) {
			entry.flags |= allocIndexSucceeded
			entry.index = index
		}
	}
	if entry.flags&allocIndexSucceeded != 0 {
		return makeIndexedStringRef(entry.index), true
	} else {
		return makeInlineStringRef([]byte(s)), true
	}
}

// ThreadRef.

type threadRef struct {
	encodedValue uint64
	processKoid  uint64
	threadKoid   uint64
}

const (
	encodedThreadRefInline   uint64 = 0
	encodedThreadRefMinIndex        = 1
	encodedThreadRefMaxIndex        = 0xff
)

const KoidInvalid uint64 = C.ZX_KOID_INVALID

func makeUnknownThreadRef() threadRef {
	return threadRef{
		encodedValue: encodedThreadRefInline,
		processKoid:  KoidInvalid,
		threadKoid:   KoidInvalid,
	}
}

func makeInlineThreadRef(processKoid uint64, threadKoid uint64) threadRef {
	return threadRef{
		encodedValue: encodedThreadRefInline,
		processKoid:  processKoid,
		threadKoid:   threadKoid,
	}
}

func makeIndexedThreadRef(index uint64) threadRef {
	return threadRef{
		encodedValue: index,
		processKoid:  KoidInvalid,
		threadKoid:   KoidInvalid,
	}
}

func (r *threadRef) IsInlineThreadRef() bool {
	return (r.encodedValue == encodedThreadRefInline) &&
		(r.processKoid != KoidInvalid || r.threadKoid != KoidInvalid)
}

func (r *threadRef) IsUnknownThreadRef() bool {
	return (r.encodedValue == encodedThreadRefInline) &&
		(r.processKoid == KoidInvalid && r.threadKoid == KoidInvalid)
}

func sizeOfEncodedThreadRef(r *threadRef) uint64 {
	if r.IsInlineThreadRef() || r.IsUnknownThreadRef() {
		return 2 * wordSize
	}
	return 0
}

func (p *payload) writeThreadRef(r *threadRef) {
	if r.IsInlineThreadRef() || r.IsUnknownThreadRef() {
		p.writeUint64(r.processKoid)
		p.writeUint64(r.threadKoid)
	}
}

// Thread record.

const threadRecordThreadIndexShift = 16

func (c *context) writeThreadRecord(index uint64, processKoid uint64, threadKoid uint64) bool {
	recordSize := recordHeaderSize + 2*wordSize
	p, ok := c.newPayload(true, recordSize)
	if !ok {
		return false
	}
	p.writeUint64(makeRecordHeader(threadT, recordSize) |
		uint64(index<<threadRecordThreadIndexShift))
	p.writeUint64(processKoid)
	p.writeUint64(threadKoid)
	return true
}

func (c *context) registerCurrentThreadLocked() threadRef {
	threadKoid := zx.GetCurrentThreadKoid()
	if entry, ok := c.mu.threadTable[threadKoid]; ok {
		return makeIndexedThreadRef(entry.index)
	}
	processKoid := GetCurrentProcessKoid()
	if index, ok := c.allocThreadIndexLocked(); ok && c.writeThreadRecord(index, processKoid, threadKoid) {
		entry := &threadEntry{
			threadKoid: threadKoid,
			index:      index,
		}
		c.mu.threadTable[threadKoid] = entry
		return makeIndexedThreadRef(entry.index)
	} else {
		return makeInlineThreadRef(processKoid, threadKoid)
	}
}

// Event record.

type eventType int

const (
	instantT          eventType = 0
	counterT                    = 1
	durationBeginT              = 2
	durationEndT                = 3
	durationCompleteT           = 4
	asyncBeginT                 = 5
	asyncInstantT               = 6
	asyncEndT                   = 7
	flowBeginT                  = 8
	flowStepT                   = 9
	flowEndT                    = 10
)

type EventScope int

const (
	ScopeThread  EventScope = C.TRACE_SCOPE_THREAD
	ScopeProcess            = C.TRACE_SCOPE_PROCESS
	ScopeGlobal             = C.TRACE_SCOPE_GLOBAL
)

const (
	eventRecordEventTypeShift         = 16
	eventRecordArgumentCountShift     = 20
	eventRecordThreadRefShift         = 24
	eventRecordCategoryStringRefShift = 32
	eventRecordNameStringRefShift     = 48
)

func (c *context) writeEventRecordBase(typ eventType, eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, args []Arg, contentSize uint64) (payload, bool) {
	recordSize := recordHeaderSize + wordSize + sizeOfEncodedThreadRef(threadRef) +
		sizeOfEncodedStringRef(categoryRef) + sizeOfEncodedStringRef(nameRef) +
		sizeOfEncodedArgs(args) + contentSize
	p, ok := c.newPayload(false, recordSize)
	if !ok {
		return payload{}, false
	}
	p.writeUint64(makeRecordHeader(eventT, recordSize) |
		uint64(typ<<eventRecordEventTypeShift) |
		uint64(len(args)<<eventRecordArgumentCountShift) |
		uint64(threadRef.encodedValue<<eventRecordThreadRefShift) |
		uint64(categoryRef.encodedValue<<eventRecordCategoryStringRefShift) |
		uint64(nameRef.encodedValue<<eventRecordNameStringRefShift))
	p.writeUint64(uint64(eventTime))
	p.writeThreadRef(threadRef)
	p.writeStringRef(categoryRef)
	p.writeStringRef(nameRef)
	p.writeArgs(args)
	return p, true
}

func (c *context) writeInstantEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, scope EventScope, args []Arg) {
	p, ok := c.writeEventRecordBase(instantT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(scope))
}

func (c *context) writeCounterEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, counterID CounterID, args []Arg) {
	p, ok := c.writeEventRecordBase(counterT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(counterID))
}

func (c *context) writeDurationEventRecord(startTime ticksT, endTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, args []Arg) {
	p, ok := c.writeEventRecordBase(durationCompleteT, startTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(endTime))
}

func (c *context) writeDurationBeginEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, args []Arg) {
	c.writeEventRecordBase(durationBeginT, eventTime, threadRef, categoryRef, nameRef, args, 0)
}

func (c *context) writeDurationEndEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, args []Arg) {
	c.writeEventRecordBase(durationEndT, eventTime, threadRef, categoryRef, nameRef, args, 0)
}

func (c *context) writeAsyncBeginEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, asyncID AsyncID, args []Arg) {
	p, ok := c.writeEventRecordBase(asyncBeginT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(asyncID))
}

func (c *context) writeAsyncInstantEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, asyncID AsyncID, args []Arg) {
	p, ok := c.writeEventRecordBase(asyncInstantT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(asyncID))
}

func (c *context) writeAsyncEndEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, asyncID AsyncID, args []Arg) {
	p, ok := c.writeEventRecordBase(asyncEndT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(asyncID))
}

func (c *context) writeFlowBeginEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, flowID FlowID, args []Arg) {
	p, ok := c.writeEventRecordBase(flowBeginT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(flowID))
}

func (c *context) writeFlowStepEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, flowID FlowID, args []Arg) {
	p, ok := c.writeEventRecordBase(flowStepT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(flowID))
}

func (c *context) writeFlowEndEventRecord(eventTime ticksT, threadRef *threadRef, categoryRef *stringRef, nameRef *stringRef, flowID FlowID, args []Arg) {
	p, ok := c.writeEventRecordBase(flowEndT, eventTime, threadRef, categoryRef, nameRef, args, wordSize)
	if !ok {
		return
	}
	p.writeUint64(uint64(flowID))
}

// Blob record.

type BlobType int

const (
	BlobTypeData       BlobType = 1
	BlobTypeLastBranch          = 2
)

const (
	blobRecordBlobNameShift = 16
	blobRecordBlobSizeShift = 32
	blobRecordBlobTypeShift = 48
)

func (c *context) writeBlobRecord(typ BlobType, nameRef *stringRef, blob []byte) (payload, bool) {
	blobSize := uint64(len(blob))
	recordSize := recordHeaderSize + sizeOfEncodedStringRef(nameRef) + roundUpToWords(blobSize)
	if recordSize > maxRecordSizeBytes {
		return payload{}, false
	}
	p, ok := c.newPayload(false, recordSize)
	if !ok {
		return payload{}, false
	}
	p.writeUint64(makeRecordHeader(blobT, recordSize) |
		uint64(typ<<blobRecordBlobTypeShift) |
		uint64(nameRef.encodedValue<<blobRecordBlobNameShift) |
		uint64(blobSize<<blobRecordBlobSizeShift))
	p.writeStringRef(nameRef)
	p.writeBytes(blob)
	return p, true
}

func init() {
	info, err := zx.ProcHandle.GetInfoHandleBasic()
	if err != nil {
		currentProcessKoid = KoidInvalid
		return
	}
	currentProcessKoid = info.Koid
}
