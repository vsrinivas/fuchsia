// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"encoding/binary"
	"fmt"
	"math"
	"sort"

	"syscall/zx"
)

// encodingState has information required to encode/decode a one-level value.
type encodingState struct {
	// Index of the first unprocessed byte.
	offset int

	// Index of the first unprocessed bit of buffer[offset] byte.
	bitOffset uint32

	// Index of the first byte after the claimed buffer block for the current
	// one-level value.
	limit int

	// Number of elements declared in the data header for the current one-level
	// value.
	elements uint32

	// Number of elements already encoded/decoded of the current one-level
	// value.
	elementsProcessed uint32

	// Whether the number of elements processed should be checked.
	checkElements bool
}

func (s *encodingState) alignOffsetToBytes() {
	if s.bitOffset > 0 {
		s.offset++
		s.bitOffset = 0
	}
}

func (s *encodingState) skipBits(count uint32) {
	s.bitOffset += count
	s.offset += int(s.bitOffset >> 3) // equal to s.bitOffset / 8
	s.bitOffset &= 7                  // equal to s.bitOffset % 8
}

func (s *encodingState) skipBytes(count int) {
	s.bitOffset = 0
	s.offset += count
}

// Encoder is a helper to encode fidl complex elements into fidl archive format.
type Encoder struct {
	// Buffer containing encoded data.
	buf []byte

	// Index of the first unclaimed byte in buf.
	end int

	// Array containing encoded handles.
	handles []zx.Handle

	// A stack of encoder states matching current one-level value stack
	// of the encoding data structure.
	stateStack []encodingState

	// By default encoding is non-deterministic because the encoding of
	// a fidl map does not specify the order of the keys and values.
	// If |deterministic| is true then keys and values will be sorted
	// in ascending key order.
	deterministic bool
}

func ensureElementBitSizeAndCapacity(state *encodingState, bitSize uint32) error {
	if state == nil {
		return fmt.Errorf("empty state stack")
	}
	if state.checkElements && state.elementsProcessed >= state.elements {
		return fmt.Errorf("can't process more than elements defined in header(%d)", state.elements)
	}
	byteSize := bytesForBits(uint64(state.bitOffset + bitSize))
	if align(state.offset+byteSize, byteSize) > state.limit {
		return fmt.Errorf("buffer size limit exceeded")
	}
	return nil
}

// claimData claims a block of |size| bytes for a one-level value, resizing
// buffer if needed.
func (e *Encoder) claimData(size int) {
	e.end += size
	if e.end < len(e.buf) {
		return
	}
	newLen := e.end
	if l := 2 * len(e.buf); newLen < l {
		newLen = l
	}
	tmp := make([]byte, newLen)
	copy(tmp, e.buf)
	e.buf = tmp
}

func (e *Encoder) popState() {
	if len(e.stateStack) != 0 {
		e.stateStack = e.stateStack[:len(e.stateStack)-1]
	}
}

func (e *Encoder) pushState(header DataHeader, checkElements bool) {
	oldEnd := e.end
	e.claimData(align(int(header.Size), defaultAlignment))
	elements := uint32(0)
	if checkElements {
		elements = header.ElementsOrVersion
	}
	e.stateStack = append(e.stateStack, encodingState{
		offset:        oldEnd,
		limit:         e.end,
		elements:      elements,
		checkElements: checkElements,
	})
}

// state returns encoder state of the top-level value.
func (e *Encoder) state() *encodingState {
	if len(e.stateStack) == 0 {
		return nil
	}
	return &e.stateStack[len(e.stateStack)-1]
}

// NewEncoder returns a new instance of encoder.
func NewEncoder() *Encoder {
	return &Encoder{}
}

// SetDeterministic sets whether or not this encoder is deterministic.
// By default encoding is non-deterministic because the encoding of
// a fidl map does not specify the order of the keys and values.
// If SetDeterministic(true) is invoked then this encoder will, from then on,
// have the property that keys and values will be sorted in ascending key order.
// Warning: This causes encoding to be more expensive.
func (e *Encoder) SetDeterministic(deterministic bool) {
	e.deterministic = deterministic
}

func (e *Encoder) Deterministic() bool {
	return e.deterministic
}

// StartArray starts encoding an array and writes its data header.
// Note: it doesn't write a pointer to the encoded array.
// Call |Finish()| after writing all array elements.
func (e *Encoder) StartArray(length, elementBitSize uint32) {
	dataSize := dataHeaderSize + bytesForBits(uint64(length)*uint64(elementBitSize))
	header := DataHeader{uint32(dataSize), length}
	e.pushState(header, true)
	e.writeDataHeader(header)
}

// StartMap starts encoding a map and writes its data header.
// Note: it doesn't write a pointer to the encoded map.
// Call |Finish()| after writing keys array and values array.
func (e *Encoder) StartMap() {
	e.pushState(mapHeader, false)
	e.writeDataHeader(mapHeader)
}

// StartStruct starts encoding a struct and writes its data header.
// Note: it doesn't write a pointer to the encoded struct.
// Call |Finish()| after writing all fields.
func (e *Encoder) StartStruct(size, version uint32) {
	dataSize := dataHeaderSize + int(size)
	header := DataHeader{uint32(dataSize), version}
	e.pushState(header, false)
	e.writeDataHeader(header)
}

// StartNestedUnion starts encoding a nested union.
// Note: it doesn't write a pointer or a union header.
// Call |Finish()| after writing all fields.
func (e *Encoder) StartNestedUnion() {
	header := DataHeader{uint32(16), uint32(0)}
	e.pushState(header, false)
}

func (e *Encoder) writeDataHeader(header DataHeader) {
	binary.LittleEndian.PutUint32(e.buf[e.state().offset:], header.Size)
	binary.LittleEndian.PutUint32(e.buf[e.state().offset+4:], header.ElementsOrVersion)
	e.state().offset += 8
}

// WriteUnionHeader writes a union header for a non-null union.
// (See. WriteNullUnion)
func (e *Encoder) WriteUnionHeader(tag uint32) error {
	if err := ensureElementBitSizeAndCapacity(e.state(), 64); err != nil {
		return err
	}
	e.state().alignOffsetToBytes()
	e.state().offset = align(e.state().offset, 8)
	binary.LittleEndian.PutUint32(e.buf[e.state().offset:], 16)
	binary.LittleEndian.PutUint32(e.buf[e.state().offset+4:], tag)
	e.state().offset += 8
	if err := ensureElementBitSizeAndCapacity(e.state(), 64); err != nil {
		return err
	}
	return nil
}

// FinishWritingUnionValue should call after the union value has been read in
// order to indicate to move the encoder past the union value field.
func (e *Encoder) FinishWritingUnionValue() {
	e.state().offset = align(e.state().offset, 8)
	e.state().alignOffsetToBytes()
}

// Finish indicates the encoder that you have finished writing elements of
// a one-level value.
func (e *Encoder) Finish() error {
	if e.state() == nil {
		return fmt.Errorf("state stack is empty")
	}
	if e.state().checkElements && e.state().elementsProcessed != e.state().elements {
		return fmt.Errorf("unexpected number of elements written: defined in header %d, but written %d", e.state().elements, e.state().elementsProcessed)
	}
	e.popState()
	return nil
}

// Data returns an encoded message with attached handles.
// Call this method after finishing encoding of a value.
func (e *Encoder) Data() ([]byte, []zx.Handle, error) {
	if len(e.stateStack) != 0 {
		return nil, nil, fmt.Errorf("can't return data when encoder has non-empty state stack")
	}
	return e.buf[:e.end], e.handles, nil
}

// WriteBool writes a bool value.
func (e *Encoder) WriteBool(value bool) error {
	if err := ensureElementBitSizeAndCapacity(e.state(), 1); err != nil {
		return err
	}
	if value {
		e.buf[e.state().offset] |= 1 << e.state().bitOffset
	}
	e.state().skipBits(1)
	e.state().elementsProcessed++
	return nil
}

// WriteBool writes an int8 value.
func (e *Encoder) WriteInt8(value int8) error {
	return e.WriteUint8(uint8(value))
}

// WriteUint8 writes an uint8 value.
func (e *Encoder) WriteUint8(value uint8) error {
	if err := ensureElementBitSizeAndCapacity(e.state(), 8); err != nil {
		return err
	}
	e.state().alignOffsetToBytes()
	e.buf[e.state().offset] = value
	e.state().skipBytes(1)
	e.state().elementsProcessed++
	return nil
}

// WriteInt16 writes an int16 value.
func (e *Encoder) WriteInt16(value int16) error {
	return e.WriteUint16(uint16(value))
}

// WriteUint16 writes an uint16 value.
func (e *Encoder) WriteUint16(value uint16) error {
	if err := ensureElementBitSizeAndCapacity(e.state(), 16); err != nil {
		return err
	}
	e.state().alignOffsetToBytes()
	e.state().offset = align(e.state().offset, 2)
	binary.LittleEndian.PutUint16(e.buf[e.state().offset:], value)
	e.state().skipBytes(2)
	e.state().elementsProcessed++
	return nil
}

// WriteInt32 writes an int32 value.
func (e *Encoder) WriteInt32(value int32) error {
	return e.WriteUint32(uint32(value))
}

// WriteUint32 writes an uint32 value.
func (e *Encoder) WriteUint32(value uint32) error {
	if err := ensureElementBitSizeAndCapacity(e.state(), 32); err != nil {
		return err
	}
	e.state().alignOffsetToBytes()
	e.state().offset = align(e.state().offset, 4)
	binary.LittleEndian.PutUint32(e.buf[e.state().offset:], value)
	e.state().skipBytes(4)
	e.state().elementsProcessed++
	return nil
}

// WriteInt64 writes an int64 value.
func (e *Encoder) WriteInt64(value int64) error {
	return e.WriteUint64(uint64(value))
}

// WriteUint64 writes an uint64 value.
func (e *Encoder) WriteUint64(value uint64) error {
	if err := ensureElementBitSizeAndCapacity(e.state(), 64); err != nil {
		return err
	}
	e.state().alignOffsetToBytes()
	e.state().offset = align(e.state().offset, 8)
	binary.LittleEndian.PutUint64(e.buf[e.state().offset:], value)
	e.state().skipBytes(8)
	e.state().elementsProcessed++
	return nil
}

// WriteFloat32 writes a float32 value.
func (e *Encoder) WriteFloat32(value float32) error {
	return e.WriteUint32(math.Float32bits(value))
}

// WriteFloat64 writes a float64 value.
func (e *Encoder) WriteFloat64(value float64) error {
	return e.WriteUint64(math.Float64bits(value))
}

// WriteNullUnion writes a null union.
func (e *Encoder) WriteNullUnion() error {
	if err := e.WriteUint64(0); err != nil {
		return err
	}
	e.state().elementsProcessed--
	return e.WriteUint64(0)
}

// WriteNullPointer writes a null pointer.
func (e *Encoder) WriteNullPointer() error {
	return e.WriteUint64(0)
}

// WriteString writes a string value. It doesn't write a pointer to the encoded
// string.
func (e *Encoder) WriteString(value string) error {
	bytes := []byte(value)
	e.StartArray(uint32(len(bytes)), 8)
	for _, b := range bytes {
		if err := e.WriteUint8(b); err != nil {
			return err
		}
	}
	return e.Finish()
}

// WritePointer writes a pointer to first unclaimed byte index.
func (e *Encoder) WritePointer() error {
	e.state().alignOffsetToBytes()
	e.state().offset = align(e.state().offset, 8)
	return e.WriteUint64(uint64(e.end - e.state().offset))
}

// WriteInvalidHandle an invalid handle.
func (e *Encoder) WriteInvalidHandle() error {
	return e.WriteInt32(-1)
}

// WriteHandle writes a handle and invalidates the passed handle object.
func (e *Encoder) WriteHandle(handle zx.Handle) error {
	if !handle.IsValid() {
		return fmt.Errorf("can't write an invalid handle")
	}
	e.handles = append(e.handles, handle)
	return e.WriteUint32(uint32(len(e.handles) - 1))
}

// WriteInvalidInterface writes an invalid interface.
func (e *Encoder) WriteInvalidInterface() error {
	if err := e.WriteInvalidHandle(); err != nil {
		return err
	}
	e.state().elementsProcessed--
	return e.WriteUint32(0)
}

// WriteInterface writes an interface and invalidates the passed handle object.
func (e *Encoder) WriteInterface(handle zx.Handle) error {
	if err := e.WriteHandle(handle); err != nil {
		return err
	}
	e.state().elementsProcessed--
	// Set the version field to 0 for now.
	return e.WriteUint32(0)
}

// SortMapKeys will sort the slice pointed to by |slicePointer|
// if |slicePointer| is a pointer to a slice of a type that
// may be the key of a Fidl map. Otherwise SortMapKeys will do nothing.
func SortMapKeys(slicePointer interface{}) {
	switch slicePointer := slicePointer.(type) {
	case *[]bool:
		sort.Sort(boolSlice(*slicePointer))
	case *[]float32:
		sort.Sort(float32Slice(*slicePointer))
	case *[]float64:
		sort.Float64s(*slicePointer)
	case *[]int:
		sort.Ints(*slicePointer)
	case *[]int8:
		sort.Sort(int8Slice(*slicePointer))
	case *[]int16:
		sort.Sort(int16Slice(*slicePointer))
	case *[]int32:
		sort.Sort(int32Slice(*slicePointer))
	case *[]int64:
		sort.Sort(int64Slice(*slicePointer))
	case *[]string:
		sort.Strings(*slicePointer)
	case *[]uint8:
		sort.Sort(uint8Slice(*slicePointer))
	case *[]uint16:
		sort.Sort(uint16Slice(*slicePointer))
	case *[]uint32:
		sort.Sort(uint32Slice(*slicePointer))
	case *[]uint64:
		sort.Sort(uint64Slice(*slicePointer))
	default:
		// Note(rudominer) Currently enums may not be used as map keys but
		// that may change in the future in which case we should handle them
		// here.
	}
}

// boolSlice
type boolSlice []bool

func (s boolSlice) Len() int {
	return len(s)
}

func (s boolSlice) Less(i, j int) bool {
	return s[i] && !s[j]
}

func (s boolSlice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// float32Slice
type float32Slice []float32

func (s float32Slice) Len() int {
	return len(s)
}

func (s float32Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s float32Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// int8Slice
type int8Slice []int8

func (s int8Slice) Len() int {
	return len(s)
}

func (s int8Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s int8Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// int16Slice
type int16Slice []int16

func (s int16Slice) Len() int {
	return len(s)
}

func (s int16Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s int16Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// int32Slice
type int32Slice []int32

func (s int32Slice) Len() int {
	return len(s)
}

func (s int32Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s int32Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// int64Slice
type int64Slice []int64

func (s int64Slice) Len() int {
	return len(s)
}

func (s int64Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s int64Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// uint8Slice
type uint8Slice []uint8

func (s uint8Slice) Len() int {
	return len(s)
}

func (s uint8Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s uint8Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// uint16Slice
type uint16Slice []uint16

func (s uint16Slice) Len() int {
	return len(s)
}

func (s uint16Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s uint16Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// uint32Slice
type uint32Slice []uint32

func (s uint32Slice) Len() int {
	return len(s)
}

func (s uint32Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s uint32Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// uint64Slice
type uint64Slice []uint64

func (s uint64Slice) Len() int {
	return len(s)
}

func (s uint64Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s uint64Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}
