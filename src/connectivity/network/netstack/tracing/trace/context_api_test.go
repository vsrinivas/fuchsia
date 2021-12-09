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

func TestPayloadWriteBytes(t *testing.T) {
	c, b := getTestContext(t)
	p, ok := c.newPayload(false, 2*wordSize)
	if !ok {
		t.Fatal("newPayload failed")
	}
	p.writeBytes([]byte("hello"))
	p.writeBytes([]byte("hello, world!"))
	compareBuffer(t, c, b, []uint64{
		nativeEndian.Uint64([]byte{'h', 'e', 'l', 'l', 'o', 0, 0, 0}),
		nativeEndian.Uint64([]byte{
			'h', 'e', 'l', 'l', 'o', ',', ' ', 'w',
			'o', 'r', 'l', 'd', '!', 0, 0, 0,
		}),
	})
}

func TestWriteArg(t *testing.T) {
	nameIndexed1 := makeIndexedStringRef(1)
	if got, want := sizeOfEncodedStringRef(&nameIndexed1), uint64(0); got != want {
		t.Errorf("Wrong size. got %d, want %d", got, want)
	}
	nameIndexed2 := makeIndexedStringRef(2)
	if sz := sizeOfEncodedStringRef(&nameIndexed2); sz != 0 {
		t.Errorf("Wrong size (%d), want 0", sz)
	}
	double1e10 := float64(1e10)

	testCases := []struct {
		testName string
		arg      Arg
		size     uint64
		buffer   []uint64
	}{
		{
			testName: "Null",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueNull(),
			},
			size: wordSize,
			buffer: []uint64{
				uint64(nullArgT) |
					1<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift,
			},
		},
		{
			testName: "int32",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueInt32(0x12345678),
			},
			size: wordSize,
			buffer: []uint64{
				uint64(int32ArgT) |
					1<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift |
					0x12345678<<argumentHeaderValueShift,
			},
		},
		{
			testName: "uint32",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueUint32(0x12345678),
			},
			size: wordSize,
			buffer: []uint64{
				uint64(uint32ArgT) |
					1<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift |
					0x12345678<<argumentHeaderValueShift,
			},
		},
		{
			testName: "int64",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueInt64(0x1234567890abcdef),
			},
			size: 2 * wordSize,
			buffer: []uint64{
				uint64(int64ArgT) |
					2<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift,
				uint64(0x1234567890abcdef),
			},
		},
		{
			testName: "uint64",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueUint64(0x1234567890abcdef),
			},
			size: 2 * wordSize,
			buffer: []uint64{
				uint64(uint64ArgT) |
					2<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift,
				uint64(0x1234567890abcdef),
			},
		},
		{
			testName: "double",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueDouble(1e10),
			},
			size: 2 * wordSize,
			buffer: []uint64{
				uint64(doubleArgT) |
					2<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift,
				*(*uint64)(unsafe.Pointer(&double1e10)),
			},
		},
		{
			testName: "stringRef",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueString(nameIndexed2),
			},
			size: wordSize,
			buffer: []uint64{
				uint64(stringArgT) |
					1<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift |
					nameIndexed2.encodedValue<<argumentHeaderValueShift,
			},
		},
		{
			testName: "pointer",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValuePointer(uintptr(8)),
			},
			size: 2 * wordSize,
			buffer: []uint64{
				uint64(pointerArgT) |
					2<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift,
				uint64(8),
			},
		},
		{
			testName: "koid",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueKoid(uint64(16)),
			},
			size: 2 * wordSize,
			buffer: []uint64{
				uint64(koidArgT) |
					2<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift,
				uint64(16),
			},
		},
		{
			testName: "bool",
			arg: Arg{
				NameRef: nameIndexed1,
				Value:   makeArgValueBool(true),
			},
			size: wordSize,
			buffer: []uint64{
				uint64(boolArgT) |
					1<<argumentHeaderSizeShift |
					nameIndexed1.encodedValue<<argumentHeaderNameShift |
					1<<argumentHeaderValueShift,
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			if got, want := sizeOfEncodedArg(&tc.arg), tc.size; got != want {
				t.Errorf("Wrong size. got %d, want %d", got, want)
			}
			p, ok := c.newPayload(false, tc.size)
			if !ok {
				t.Fatal("newPayload failed")
			}
			p.writeArg(&tc.arg)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}

func TestInitializationRecord(t *testing.T) {
	testCases := []struct {
		testName string
		tps      uint64
		buffer   []uint64
	}{
		{
			testName: "100000",
			tps:      100000,
			buffer: []uint64{
				uint64(initializationT) | 2<<recordHeaderSizeShift,
				uint64(100000),
			},
		},
		{
			testName: "0x1234567890abcdef",
			tps:      0x1234567890abcdef,
			buffer: []uint64{
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

func TestWriteStringRef(t *testing.T) {
	testCases := []struct {
		testName string
		sref     stringRef
		size     uint64
		buffer   []uint64
	}{
		{
			testName: "Inline short",
			sref:     makeInlineStringRef([]byte("short")),
			size:     wordSize,
			buffer: []uint64{
				nativeEndian.Uint64([]byte{'s', 'h', 'o', 'r', 't', 0, 0, 0}),
			},
		},
		{
			testName: "Indexed",
			sref:     makeIndexedStringRef(1),
			size:     0,
			buffer:   nil,
		},
		{
			testName: "Inline long",
			sref:     makeInlineStringRef([]byte("This is a long string.")),
			size:     3 * wordSize,
			buffer: []uint64{
				nativeEndian.Uint64([]byte{'T', 'h', 'i', 's', ' ', 'i', 's', ' '}),
				nativeEndian.Uint64([]byte{'a', ' ', 'l', 'o', 'n', 'g', ' ', 's'}),
				nativeEndian.Uint64([]byte{'t', 'r', 'i', 'n', 'g', '.', 0, 0}),
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			if got, want := sizeOfEncodedStringRef(&tc.sref), tc.size; got != want {
				t.Errorf("Wrong size. got %d, want %d", got, want)
			}
			p, ok := c.newPayload(false, tc.size)
			if !ok {
				t.Fatal("newPayload failed")
			}
			p.writeStringRef(&tc.sref)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}

func TestWriteStringRecord(t *testing.T) {
	testCases := []struct {
		testName  string
		isDurable bool
		index     uint64
		str       []byte
		buffer    []uint64
	}{
		{
			testName:  "short",
			isDurable: false,
			index:     1,
			str:       []byte("short"),
			buffer: []uint64{
				uint64(stringT) |
					2<<recordHeaderSizeShift |
					1<<stringRecordStringIndexShift |
					uint64(len("short"))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'s', 'h', 'o', 'r', 't', 0, 0, 0}),
			},
		},
		{
			testName:  "This is a long string.",
			isDurable: false,
			index:     10,
			str:       []byte("This is a long string."),
			buffer: []uint64{
				uint64(stringT) |
					4<<recordHeaderSizeShift |
					10<<stringRecordStringIndexShift |
					uint64(len("This is a long string."))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'T', 'h', 'i', 's', ' ', 'i', 's', ' '}),
				nativeEndian.Uint64([]byte{'a', ' ', 'l', 'o', 'n', 'g', ' ', 's'}),
				nativeEndian.Uint64([]byte{'t', 'r', 'i', 'n', 'g', '.', 0, 0}),
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			c.writeStringRecord(tc.isDurable, tc.index, tc.str)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}

func TestRegisterStringLocked(t *testing.T) {
	type args struct {
		s                 string
		checkCategory     bool
		isCategoryEnabled bool
		isInlineStringRef bool
	}
	testCases := []struct {
		testName       string
		initCategories []string
		argsList       []args
		buffer         []uint64
	}{
		{
			testName:       "two duplicate inputs",
			initCategories: nil,
			argsList: []args{
				{"hello", false, true, false},
				{"hello", false, true, false},
				{"goodbye", false, true, false},
				{"goodbye", false, true, false},
			},
			buffer: []uint64{
				uint64(stringT) |
					2<<recordHeaderSizeShift |
					1<<stringRecordStringIndexShift |
					uint64(len("hello"))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'h', 'e', 'l', 'l', 'o', 0, 0, 0}),
				uint64(stringT) |
					2<<recordHeaderSizeShift |
					2<<stringRecordStringIndexShift |
					uint64(len("goodbye"))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'g', 'o', 'o', 'd', 'b', 'y', 'e', 0}),
			},
		},
		{
			testName:       "check category",
			initCategories: []string{"foo"},
			argsList: []args{
				{"foo", true, true, false},
				{"bar", true, false, false /* not used */},
			},
			buffer: []uint64{
				uint64(stringT) |
					2<<recordHeaderSizeShift |
					1<<stringRecordStringIndexShift |
					uint64(len("foo"))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'f', 'o', 'o', 0, 0, 0, 0, 0}),
			},
		},
		{
			testName:       "check category, no initial categories",
			initCategories: nil,
			argsList: []args{
				{"foo", true, true, false},
				{"bar", true, true, false},
			},
			buffer: []uint64{
				uint64(stringT) |
					2<<recordHeaderSizeShift |
					1<<stringRecordStringIndexShift |
					uint64(len("foo"))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'f', 'o', 'o', 0, 0, 0, 0, 0}),
				uint64(stringT) |
					2<<recordHeaderSizeShift |
					2<<stringRecordStringIndexShift |
					uint64(len("bar"))<<stringRecordStringLengthShift,
				nativeEndian.Uint64([]byte{'b', 'a', 'r', 0, 0, 0, 0, 0}),
			},
		},
	}
	for _, tc := range testCases {
		c, b := getTestContext(t)
		c.handler.(*testSession).setCategories(tc.initCategories)
		for _, args := range tc.argsList {
			sref, enabled := c.registerStringLocked(args.s, args.checkCategory)
			if got, want := enabled, args.isCategoryEnabled; got != want {
				t.Errorf("%s: got enabled = %t (want = %t)", args.s, got, want)
			}
			if !enabled {
				continue
			}
			if got, want := sref.IsInlineStringRef(), args.isInlineStringRef; got != want {
				t.Errorf("%s: got sref.IsInlineStringRef() = %t (want = %t)", args.s, got, want)
			}
		}
		compareBuffer(t, c, b, tc.buffer)
	}
}

func TestWriteThreadRef(t *testing.T) {
	testCases := []struct {
		testName string
		thref    threadRef
		size     uint64
		buffer   []uint64
	}{
		{
			testName: "Unknown",
			thref:    makeUnknownThreadRef(),
			size:     2 * wordSize,
			buffer: []uint64{
				0,
				0,
			},
		},
		{
			testName: "Inline",
			thref:    makeInlineThreadRef(1000, 2000),
			size:     2 * wordSize,
			buffer: []uint64{
				1000,
				2000,
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			if got, want := sizeOfEncodedThreadRef(&tc.thref), tc.size; got != want {
				t.Errorf("Wrong size. got %d, want %d", got, want)
			}
			p, ok := c.newPayload(false, tc.size)
			if !ok {
				t.Fatal("newPayload failed")
			}
			p.writeThreadRef(&tc.thref)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}

func TestWriteThreadRecord(t *testing.T) {
	testCases := []struct {
		testName    string
		index       uint64
		processKoid uint64
		threadKoid  uint64
		buffer      []uint64
	}{
		{
			testName:    "1000-2000",
			index:       1,
			processKoid: 1000,
			threadKoid:  2000,
			buffer: []uint64{
				uint64(threadT) |
					3<<recordHeaderSizeShift |
					1<<threadRecordThreadIndexShift,
				uint64(1000),
				uint64(2000),
			},
		},
		{
			testName:    "0x1234567890abcdef-0xfedcba0987654321",
			index:       1,
			processKoid: 0x1234567890abcdef,
			threadKoid:  0xfedcba0987654321,
			buffer: []uint64{
				uint64(threadT) |
					3<<recordHeaderSizeShift |
					1<<threadRecordThreadIndexShift,
				uint64(0x1234567890abcdef),
				uint64(0xfedcba0987654321),
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			c.writeThreadRecord(tc.index, tc.processKoid, tc.threadKoid)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}

func TestWriteEventRecord(t *testing.T) {
	const (
		ticks        = uint64(100)
		ticks2       = uint64(200)
		threadIndex  = uint64(1)
		processKoid  = uint64(1000)
		threadKoid   = uint64(2000)
		strNetIndex  = uint64(10)
		strSendIndex = uint64(11)
		strPortIndex = uint64(12)
		counterID    = uint64(1234)
		asyncID      = uint64(5678)
		flowID       = uint64(9012)
		argInt32     = int32(10000)
	)
	indexedTh := makeIndexedThreadRef(threadIndex)
	inlineTh := makeInlineThreadRef(processKoid, threadKoid)
	indexedStrNet := makeIndexedStringRef(strNetIndex)
	inlineStrNet := makeInlineStringRef([]byte("net"))
	indexedStrSend := makeIndexedStringRef(strSendIndex)
	inlineStrSend := makeInlineStringRef([]byte("send"))
	inlineStrPort := makeInlineStringRef([]byte("port"))

	testCases := []struct {
		testName string
		thref    threadRef
		category stringRef
		name     stringRef
		args     []Arg
	}{
		{
			testName: "No Args, indexed thread ref, indexed string refs",
			thref:    indexedTh,
			category: indexedStrNet,
			name:     indexedStrSend,
			args:     nil,
		},
		{
			testName: "No Args, inline thread ref, inline string refs",
			thref:    inlineTh,
			category: inlineStrNet,
			name:     inlineStrSend,
			args:     nil,
		},
		{
			testName: "With Args, inline thread ref, inline string refs",
			thref:    inlineTh,
			category: inlineStrNet,
			name:     inlineStrSend,
			args: []Arg{
				{
					NameRef: inlineStrPort,
					Value:   makeArgValueInt32(argInt32),
				},
			},
		},
	}

	eventFuncs := []struct {
		name    string
		f       func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg)
		buffers [][]uint64
	}{
		{
			name: "Instant",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeInstantEventRecord(ticksT(ticks), thref, category, name, ScopeProcess, args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(instantT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					uint64(ScopeProcess),
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(instantT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(ScopeProcess),
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(instantT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					uint64(ScopeProcess),
				},
			},
		},
		{
			name: "Counter",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeCounterEventRecord(ticksT(ticks), thref, category, name, CounterID(counterID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(counterT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					counterID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(counterT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					counterID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(counterT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					counterID,
				},
			},
		},
		{
			name: "DurationBegin",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeDurationBeginEventRecord(ticksT(ticks), thref, category, name, args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						2<<recordHeaderSizeShift |
						uint64(durationBeginT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						6<<recordHeaderSizeShift |
						uint64(durationBeginT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						8<<recordHeaderSizeShift |
						uint64(durationBeginT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
				},
			},
		},
		{
			name: "DurationEnd",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeDurationEndEventRecord(ticksT(ticks), thref, category, name, args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						2<<recordHeaderSizeShift |
						uint64(durationEndT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						6<<recordHeaderSizeShift |
						uint64(durationEndT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						8<<recordHeaderSizeShift |
						uint64(durationEndT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
				},
			},
		},
		{
			name: "DurationComplete",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeDurationEventRecord(ticksT(ticks), ticksT(ticks2), thref, category, name, args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(durationCompleteT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					ticks2,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(durationCompleteT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					ticks2,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(durationCompleteT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					ticks2,
				},
			},
		},
		{
			name: "AsyncBegin",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeAsyncBeginEventRecord(ticksT(ticks), thref, category, name, AsyncID(asyncID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(asyncBeginT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					asyncID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(asyncBeginT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					asyncID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(asyncBeginT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					asyncID,
				},
			},
		},
		{
			name: "AsyncInstant",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeAsyncInstantEventRecord(ticksT(ticks), thref, category, name, AsyncID(asyncID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(asyncInstantT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					asyncID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(asyncInstantT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					asyncID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(asyncInstantT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					asyncID,
				},
			},
		},
		{
			name: "AsyncEnd",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeAsyncEndEventRecord(ticksT(ticks), thref, category, name, AsyncID(asyncID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(asyncEndT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					asyncID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(asyncEndT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					asyncID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(asyncEndT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					asyncID,
				},
			},
		},
		{
			name: "FlowBegin",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeFlowBeginEventRecord(ticksT(ticks), thref, category, name, FlowID(flowID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(flowBeginT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					flowID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(flowBeginT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					flowID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(flowBeginT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					flowID,
				},
			},
		},
		{
			name: "FlowStep",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeFlowStepEventRecord(ticksT(ticks), thref, category, name, FlowID(flowID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(flowStepT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks,
					flowID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(flowStepT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					flowID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(flowStepT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					flowID,
				},
			},
		},
		{
			name: "FlowEnd",
			f: func(c *context, thref *threadRef, category *stringRef, name *stringRef, args []Arg) {
				c.writeFlowEndEventRecord(ticksT(ticks), thref, category, name, FlowID(flowID), args)
			},
			buffers: [][]uint64{
				{ // No Args, indexed thread ref, indexed string refs
					uint64(eventT) |
						3<<recordHeaderSizeShift |
						uint64(flowEndT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						threadIndex<<eventRecordThreadRefShift |
						strNetIndex<<eventRecordCategoryStringRefShift |
						strSendIndex<<eventRecordNameStringRefShift,
					ticks, flowID,
				},
				{ // No Args, inline thread ref, inline string refs
					uint64(eventT) |
						7<<recordHeaderSizeShift |
						uint64(flowEndT)<<eventRecordEventTypeShift |
						uint64(0)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					flowID,
				},
				{ // With Args, inline thread ref, inline string refs
					uint64(eventT) |
						9<<recordHeaderSizeShift |
						uint64(flowEndT)<<eventRecordEventTypeShift |
						uint64(1)<<eventRecordArgumentCountShift |
						inlineTh.encodedValue<<eventRecordThreadRefShift |
						inlineStrNet.encodedValue<<eventRecordCategoryStringRefShift |
						inlineStrSend.encodedValue<<eventRecordNameStringRefShift,
					ticks,
					processKoid,
					threadKoid,
					nativeEndian.Uint64([]byte{'n', 'e', 't', 0, 0, 0, 0, 0}),
					nativeEndian.Uint64([]byte{'s', 'e', 'n', 'd', 0, 0, 0, 0}),
					uint64(int32ArgT) |
						2<<argumentHeaderSizeShift |
						inlineStrPort.encodedValue<<argumentHeaderNameShift |
						uint64(argInt32)<<argumentHeaderValueShift,
					nativeEndian.Uint64([]byte{'p', 'o', 'r', 't', 0, 0, 0, 0}),
					flowID,
				},
			},
		},
	}
	for _, ev := range eventFuncs {
		for i, tc := range testCases {
			t.Run(ev.name+"_"+tc.testName, func(t *testing.T) {
				c, b := getTestContext(t)
				ev.f(c, &tc.thref, &tc.category, &tc.name, tc.args)
				compareBuffer(t, c, b, ev.buffers[i])
			})
		}
	}
}

func TestWriteBlobRecord(t *testing.T) {
	strPi := makeInlineStringRef([]byte("pi"))
	blobPi := []byte{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 9}

	testCases := []struct {
		testName string
		blobType BlobType
		name     stringRef
		blob     []byte
		buffer   []uint64
	}{
		{
			testName: "Pi",
			blobType: BlobTypeData,
			name:     strPi,
			blob:     blobPi,
			buffer: []uint64{
				uint64(blobT) |
					4<<recordHeaderSizeShift |
					strPi.encodedValue<<blobRecordBlobNameShift |
					uint64(len(blobPi))<<blobRecordBlobSizeShift |
					uint64(BlobTypeData)<<blobRecordBlobTypeShift,
				nativeEndian.Uint64([]byte{'p', 'i', 0, 0, 0, 0, 0, 0}),
				nativeEndian.Uint64([]byte{3, 1, 4, 1, 5, 9, 2, 6}),
				nativeEndian.Uint64([]byte{5, 3, 5, 9, 0, 0, 0, 0}),
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			c, b := getTestContext(t)
			c.writeBlobRecord(tc.blobType, &tc.name, tc.blob)
			compareBuffer(t, c, b, tc.buffer)
		})
	}
}
