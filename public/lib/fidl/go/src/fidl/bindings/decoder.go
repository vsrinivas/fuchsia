// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"encoding/binary"
	"fmt"
	"math"

	"syscall/zx"
)

// Decoder is a helper to decode fidl complex elements from fidl archive format.
type Decoder struct {
	// Buffer containing data to decode.
	buf []byte

	// Index of the first unclaimed byte in buf.
	end int

	// Array containing handles to decode.
	handles []zx.Handle

	// The first unclaimed handle index.
	nextHandle int

	// A stack of encoding states matching current one-level value stack
	// of the decoding data structure.
	stateStack []encodingState
}

// NewDecoder returns a decoder that will decode structured data from provided
// byte array and with handles.
func NewDecoder(bytes []byte, handles []zx.Handle) *Decoder {
	return &Decoder{buf: bytes, handles: handles}
}

// claimData claims a block of |size| bytes for a one-level value.
func (d *Decoder) claimData(size int) error {
	if d.end+size > len(d.buf) {
		return &ValidationError{IllegalMemoryRange, "data buffer is too small"}
	}
	d.end += size
	return nil
}

func (d *Decoder) claimHandle(index int) (zx.Handle, error) {
	if index >= len(d.handles) {
		return 0, &ValidationError{IllegalHandle, "trying to access non present handle"}
	}
	if index < d.nextHandle {
		return 0, &ValidationError{IllegalHandle, "trying to access handle out of order"}
	}
	d.nextHandle = index + 1
	return d.handles[index], nil
}

func (d *Decoder) popState() {
	if len(d.stateStack) != 0 {
		d.stateStack = d.stateStack[:len(d.stateStack)-1]
	}
}

func (d *Decoder) pushState(header DataHeader, checkElements bool) error {
	oldEnd := d.end
	if err := d.claimData(int(header.Size - dataHeaderSize)); err != nil {
		return err
	}
	elements := uint32(0)
	if checkElements {
		elements = header.ElementsOrVersion
	}
	d.stateStack = append(d.stateStack, encodingState{
		offset:        oldEnd,
		limit:         d.end,
		elements:      elements,
		checkElements: checkElements,
	})
	return nil
}

// state returns state of the top-level value.
func (d *Decoder) state() *encodingState {
	if len(d.stateStack) == 0 {
		return nil
	}
	return &d.stateStack[len(d.stateStack)-1]
}

// StartArray starts decoding an array and reads its data header,
// returning number of elements declared in data header.
// Note: it doesn't read a pointer to the encoded array.
// Call |Finish()| after reading all array elements.
func (d *Decoder) StartArray(elementBitSize uint32) (uint32, error) {
	header, err := d.readDataHeader()
	if err != nil {
		return 0, err
	}
	minSize := bytesForBits(uint64(header.ElementsOrVersion) * uint64(elementBitSize))
	if got, want := int(header.Size), dataHeaderSize+minSize; got < want {
		return 0, &ValidationError{UnexpectedArrayHeader,
			fmt.Sprintf("data header size(%d) should be at least %d", got, want),
		}
	}
	if err := d.pushState(header, true); err != nil {
		return 0, err
	}
	return header.ElementsOrVersion, nil
}

// StartMap starts decoding a map and reads its data header.
// Note: it doesn't read a pointer to the encoded map.
// Call |Finish()| after reading keys array and values array.
func (d *Decoder) StartMap() error {
	header, err := d.readDataHeader()
	if err != nil {
		return err
	}
	if header != mapHeader {
		return &ValidationError{UnexpectedStructHeader,
			fmt.Sprintf("invalid map header: %v", header),
		}
	}
	if err := d.pushState(header, false); err != nil {
		return err
	}
	return nil
}

// StartStruct starts decoding a struct and reads its data header.
// Returns the read data header. The caller should check if it is valid.
// Note: it doesn't read a pointer to the encoded struct.
// Call |Finish()| after reading all fields.
func (d *Decoder) StartStruct() (DataHeader, error) {
	header, err := d.readDataHeader()
	if err != nil {
		return DataHeader{}, err
	}
	if header.Size < dataHeaderSize {
		return DataHeader{}, &ValidationError{UnexpectedStructHeader,
			fmt.Sprintf("data header size(%d) should be at least %d", header.Size, dataHeaderSize),
		}
	}
	if err := d.pushState(header, false); err != nil {
		return DataHeader{}, err
	}
	return header, nil
}

// StartNestedUnion starts decoding a union.
// Note: it doesn't read a pointer to the encoded struct or the union header.
// Call |Finish()| after reading the header and data.
func (d *Decoder) StartNestedUnion() error {
	// We have to trick pushState into claiming 16 bytes.
	header := DataHeader{uint32(24), uint32(0)}
	if err := d.pushState(header, false); err != nil {
		return err
	}
	return nil
}

// ReadUnionHeader reads the union header and returns the union's size and tag.
func (d *Decoder) ReadUnionHeader() (uint32, uint32, error) {
	if err := ensureElementBitSizeAndCapacity(d.state(), 64); err != nil {
		return 0, 0, err
	}
	d.state().alignOffsetToBytes()
	d.state().offset = align(d.state().offset, 8)
	size := binary.LittleEndian.Uint32(d.buf[d.state().offset:])
	tag := binary.LittleEndian.Uint32(d.buf[d.state().offset+4:])
	d.state().offset += 8
	if err := ensureElementBitSizeAndCapacity(d.state(), 64); err != nil {
		return 0, 0, err
	}
	return size, tag, nil
}

// FinishReadingUnionValue should be called after the union value has been read
// in order to indicate to move the decoder past the union value field.
func (d *Decoder) FinishReadingUnionValue() {
	d.state().offset = align(d.state().offset, 8)
	d.state().alignOffsetToBytes()
}

// SkipUnionValue skips the union's value.
func (d *Decoder) SkipUnionValue() {
	d.state().offset += 8
	d.state().elementsProcessed += 1
}

func (d *Decoder) readDataHeader() (DataHeader, error) {
	if err := d.claimData(dataHeaderSize); err != nil {
		return DataHeader{}, err
	}
	oldEnd := d.end - dataHeaderSize
	header := DataHeader{
		Size:              binary.LittleEndian.Uint32(d.buf[oldEnd:]),
		ElementsOrVersion: binary.LittleEndian.Uint32(d.buf[oldEnd+4:]),
	}
	return header, nil
}

// Finish indicates the decoder that you have finished reading elements of
// a one-level value.
func (d *Decoder) Finish() error {
	if d.state() == nil {
		return fmt.Errorf("state stack is empty")
	}
	if d.state().checkElements && d.state().elementsProcessed != d.state().elements {
		return fmt.Errorf("unexpected number of elements read: defined in header %d, but read %d", d.state().elements, d.state().elementsProcessed)
	}
	d.popState()
	return nil
}

// ReadBool reads a bool value.
func (d *Decoder) ReadBool() (bool, error) {
	if err := ensureElementBitSizeAndCapacity(d.state(), 1); err != nil {
		return false, err
	}
	value := ((d.buf[d.state().offset] >> d.state().bitOffset) & 1) == 1
	d.state().skipBits(1)
	d.state().elementsProcessed++
	return value, nil
}

// ReadInt8 reads an int8 value.
func (d *Decoder) ReadInt8() (int8, error) {
	value, err := d.ReadUint8()
	return int8(value), err
}

// ReadUint8 reads an uint8 value.
func (d *Decoder) ReadUint8() (uint8, error) {
	if err := ensureElementBitSizeAndCapacity(d.state(), 8); err != nil {
		return 0, err
	}
	d.state().alignOffsetToBytes()
	value := d.buf[d.state().offset]
	d.state().skipBytes(1)
	d.state().elementsProcessed++
	return value, nil
}

// ReadInt16 reads an int16 value.
func (d *Decoder) ReadInt16() (int16, error) {
	value, err := d.ReadUint16()
	return int16(value), err
}

// ReadUint16 reads an uint16 value.
func (d *Decoder) ReadUint16() (uint16, error) {
	if err := ensureElementBitSizeAndCapacity(d.state(), 16); err != nil {
		return 0, err
	}
	d.state().alignOffsetToBytes()
	d.state().offset = align(d.state().offset, 2)
	value := binary.LittleEndian.Uint16(d.buf[d.state().offset:])
	d.state().skipBytes(2)
	d.state().elementsProcessed++
	return value, nil
}

// ReadInt32 reads an int32 value.
func (d *Decoder) ReadInt32() (int32, error) {
	value, err := d.ReadUint32()
	return int32(value), err
}

// ReadUint32 reads an uint32 value.
func (d *Decoder) ReadUint32() (uint32, error) {
	if err := ensureElementBitSizeAndCapacity(d.state(), 32); err != nil {
		return 0, err
	}
	d.state().alignOffsetToBytes()
	d.state().offset = align(d.state().offset, 4)
	value := binary.LittleEndian.Uint32(d.buf[d.state().offset:])
	d.state().skipBytes(4)
	d.state().elementsProcessed++
	return value, nil
}

// ReadInt64 reads an int64 value.
func (d *Decoder) ReadInt64() (int64, error) {
	value, err := d.ReadUint64()
	return int64(value), err
}

// ReadUint64 reads an uint64 value.
func (d *Decoder) ReadUint64() (uint64, error) {
	if err := ensureElementBitSizeAndCapacity(d.state(), 64); err != nil {
		return 0, err
	}
	d.state().alignOffsetToBytes()
	d.state().offset = align(d.state().offset, 8)
	value := binary.LittleEndian.Uint64(d.buf[d.state().offset:])
	d.state().skipBytes(8)
	d.state().elementsProcessed++
	return value, nil
}

// ReadFloat32 reads a float32 value.
func (d *Decoder) ReadFloat32() (float32, error) {
	bits, err := d.ReadUint32()
	return math.Float32frombits(bits), err
}

// ReadFloat64 reads a float64 value.
func (d *Decoder) ReadFloat64() (float64, error) {
	bits, err := d.ReadUint64()
	return math.Float64frombits(bits), err
}

// ReadString reads a string value. It doesn't read a pointer to the encoded
// string.
func (d *Decoder) ReadString() (string, error) {
	length, err := d.StartArray(8)
	if err != nil {
		return "", err
	}
	var bytes []byte
	for i := uint32(0); i < length; i++ {
		b, err := d.ReadUint8()
		if err != nil {
			return "", err
		}
		bytes = append(bytes, b)
	}
	if err := d.Finish(); err != nil {
		return "", err
	}
	return string(bytes), nil
}

// ReadPointer reads a pointer and reassigns first unclaimed byte index if the
// pointer is not null.
func (d *Decoder) ReadPointer() (uint64, error) {
	pointer, err := d.ReadUint64()
	if err != nil {
		return pointer, err
	}
	if pointer == 0 {
		return pointer, nil
	}

	newEnd := uint64(d.state().offset-8) + pointer
	if pointer >= uint64(len(d.buf)) || newEnd >= uint64(len(d.buf)) {
		return 0, &ValidationError{IllegalPointer, "trying to access out of range memory"}
	}
	if newEnd < uint64(d.end) {
		return 0, &ValidationError{IllegalMemoryRange, "trying to access memory out of order"}
	}
	if newEnd%8 != 0 {
		return 0, &ValidationError{MisalignedObject,
			fmt.Sprintf("incorrect pointer data alignment: %d", newEnd),
		}
	}
	d.claimData(int(newEnd) - d.end)
	return pointer, nil
}

// ReadUntypedHandle reads an untyped handle.
func (d *Decoder) ReadUntypedHandle() (zx.Handle, error) {
	handleIndex, err := d.ReadUint32()
	if err != nil {
		return 0, err
	}
	if handleIndex == ^uint32(0) {
		return 0, nil
	}
	return d.claimHandle(int(handleIndex))
}

// ReadHandle reads a handle.
func (d *Decoder) ReadHandle() (zx.Handle, error) {
	return d.ReadUntypedHandle()
}

// ReadChannelHandle reads a channel handle.
func (d *Decoder) ReadChannelHandle() (zx.Handle, error) {
	if handle, err := d.ReadUntypedHandle(); err != nil {
		return 0, err
	} else {
		return handle, nil
	}
}

// ReadVmoHandle reads a shared buffer handle.
func (d *Decoder) ReadVmoHandle() (zx.Handle, error) {
	if handle, err := d.ReadUntypedHandle(); err != nil {
		return 0, err
	} else {
		return handle, nil
	}
}

// ReadInterface reads an encoded interface and returns the channel handle.
// The version field is ignored for now.
func (d *Decoder) ReadInterface() (zx.Handle, error) {
	handle, err := d.ReadChannelHandle()
	if err != nil {
		return 0, err
	}
	d.state().elementsProcessed--
	if _, err := d.ReadUint32(); err != nil {
		return 0, err
	}
	return handle, nil
}
