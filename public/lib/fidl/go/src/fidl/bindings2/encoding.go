// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

// TODO(mknyszek): Support unions and interfaces.
// TODO(mknyszek): Create a proper set of error values.

import (
	"errors"
	"fmt"
	"math"
	"reflect"
	"strconv"
	"strings"
	"syscall/zx"
)

const (
	allocPresent uint64 = math.MaxUint64
	noAlloc             = 0
)

const (
	handlePresent uint32 = math.MaxUint32
	noHandle             = 0
)

var (
	// TODO(mknyszek): Add support here for process, thread, job, resource,
	// interrupt, eventpair, fifo, guest, and time once these are actually
	// supported in the Go runtime.
	// TODO(mknyszek): Add channel, log, and port once the underlying types
	// are unified to uint32.
	handleType reflect.Type = reflect.TypeOf(zx.Handle(0))
	vmoType                 = reflect.TypeOf(zx.VMO(0))
	eventType               = reflect.TypeOf(zx.Event(0))
	socketType              = reflect.TypeOf(zx.Socket(0))
	vmarType                = reflect.TypeOf(zx.VMAR(0))
	proxyType               = reflect.TypeOf(Proxy{})
)

// isHandleType returns true if the reflected type is a Fuchsia handle type.
func isHandleType(t reflect.Type) bool {
	switch t {
	case handleType:
		fallthrough
	case vmoType:
		fallthrough
	case eventType:
		fallthrough
	case socketType:
		fallthrough
	case vmarType:
		return true
	}
	return false
}

// isInterfaceType returns true if the reflected type is a FIDL interface type.
func isInterfaceType(t reflect.Type) bool {
	// FIDL interfaces are represented as aliases over Proxy.
	return t.ConvertibleTo(proxyType)
}

// getSize returns the size of the type. Occasionally this requires the value,
// particularly for a struct.
func getSize(t reflect.Type, v reflect.Value) (int, error) {
	switch t.Kind() {
	case reflect.Array:
		if v.Len() == 0 {
			return 0, nil
		}
		s, err := getSize(t.Elem(), v.Index(0))
		if err != nil {
			return 0, err
		}
		return v.Len() * s, nil
	case reflect.Bool, reflect.Int8, reflect.Uint8:
		return 1, nil
	case reflect.Int16, reflect.Uint16:
		return 2, nil
	case reflect.Int32, reflect.Uint32, reflect.Float32:
		return 4, nil
	case reflect.Int64, reflect.Uint64, reflect.Float64:
		return 8, nil
	case reflect.Ptr:
		i := t.Elem()
		switch i.Kind() {
		case reflect.Slice:
			return 16, nil
		case reflect.String:
			return 16, nil
		}
		return 0, fmt.Errorf("unsupported pointer type kind %s for type %s", t.Kind(), t.Name())
	case reflect.String:
		return 16, nil
	case reflect.Slice:
		return 16, nil
	case reflect.Struct:
		return getStructSize(t, v)
	}
	return 0, fmt.Errorf("unsupported inline type kind %s for type %s", t.Kind(), t.Name())
}

func structAsPayload(t reflect.Type, v reflect.Value) (Payload, error) {
	// Get the size and alignment for the struct.
	//
	// Note that Addr can fail if the originally derived value is not "addressable",
	// meaning the root ValueOf() call was on a struct value, not a pointer. However,
	// we guarantee the struct is addressable by forcing a Payload to be passed in
	// (a struct value will never cast as an interface).
	//
	// We avoid using Implements(), MethodByName(), and Call() here because they're
	// very slow.
	payload, ok := v.Addr().Interface().(Payload)
	if !ok {
		return nil, fmt.Errorf("struct %s must implement Payload", t.Name())
	}
	return payload, nil
}

func getStructSize(t reflect.Type, v reflect.Value) (int, error) {
	p, err := structAsPayload(t, v)
	if err != nil {
		return 0, err
	}
	return p.InlineSize(), nil
}

func getStructAlignment(t reflect.Type, v reflect.Value) (int, error) {
	p, err := structAsPayload(t, v)
	if err != nil {
		return 0, err
	}
	return p.InlineAlignment(), nil
}

// fieldData contains metadata for a single struct field for use during encoding and
// decoding. It is derived from a struct field tag, and generally contains facilities
// for managing state in a recursive context (since even the type in a single struct field
// may be recursively defined).
type nestedTypeData struct {
	// maxElems represents the maximum number of elements allowed for a
	// container type. "nil" means there is no maximum. It is represented
	// as a slice because FIDL container types may be nested arbitrarily
	// deeply. The lower the index, the more deeply nested the type is.
	maxElems []*int

	// nullable reflects whether the innermost type for a struct field is nullable.
	// This is only used for types where nullability is non-obvious, which is only
	// handles for now.
	nullable bool
}

// Unnest attempts to unnest the nestedTypeData one level. If it succeeds, it returns the type
// data for that nesting level, and modifies the data structure for the next nested container.
func (n *nestedTypeData) Unnest() *int {
	if len(n.maxElems) == 0 {
		return nil
	}
	v := n.maxElems[len(n.maxElems)-1]
	n.maxElems = n.maxElems[:len(n.maxElems)-1]
	return v
}

// FromTag derives metadata from data serialized into a golang struct field
// tag.
func (n *nestedTypeData) FromTag(tag reflect.StructTag) error {
	raw, ok := tag.Lookup("fidl")
	if !ok {
		return nil
	}
	split := strings.Split(raw, ",")
	if split[0] == "*" {
		n.nullable = true
		split = split[1:]
	}
	var maxElems []*int
	for _, e := range split {
		if e == "" {
			maxElems = append(maxElems, nil)
			continue
		}
		i, err := strconv.ParseInt(e, 0, 64)
		if err != nil {
			return err
		}
		val := int(i)
		maxElems = append(maxElems, &val)
	}
	n.maxElems = maxElems
	return nil
}

// align increases size such that size is aligned to bytes, and returns the new size.
//
// bytes must be a power of 2.
func align(size, bytes int) int {
	offset := size & (bytes - 1)
	// If we're not currently aligned to |bytes| bytes, add padding.
	if offset != 0 {
		size += (bytes - offset)
	}
	return size
}

// encoder represents the encoding context that is necessary to maintain across
// recursive calls within the same FIDL object.
type encoder struct {
	// head is the index into buffer at which new data will be written to for the current
	// object. It must be updated before writing to a new out-of-line object, and then
	// fixed when that object is finished.
	head int

	// buffer represents the output buffer that the encoder writes into.
	buffer []byte

	// handles are the handles discovered when traversing the FIDL data
	// structure. They are referenced from within the serialized data
	// structure in buffer.
	handles []zx.Handle
}

func (e *encoder) newObject(size int) int {
	size = align(size, 8)
	start := len(e.buffer)
	e.buffer = append(e.buffer, make([]byte, size)...)
	return start
}

// writeInt writes an integer of byte-width size to the buffer.
//
// Before writing, it pads the buffer such that the integer is aligned to
// its own byte-width.
//
// size must be a power of 2 <= 8.
func (e *encoder) writeInt(val int64, size int) {
	e.writeUint(uint64(val), size)
}

// writeUint writes an unsigned integer of byte-width size to the buffer.
//
// Before writing, it pads the buffer such that the integer is aligned to
// its own byte-width.
//
// size must be a power of 2 <= 8.
func (e *encoder) writeUint(val uint64, size int) {
	e.head = align(e.head, size)
	for i := e.head; i < e.head+size; i++ {
		e.buffer[i] = byte(val & 0xFF)
		val >>= 8
	}
	e.head += size
}

// marshalStructInline first aligns head to the struct's alignment factor, and
// then writes its fields inline.
//
// Expects the Type t and Value v to refer to a struct value, not a pointer.
func (e *encoder) marshalStructInline(t reflect.Type, v reflect.Value) error {
	a, err := getStructAlignment(t, v)
	if err != nil {
		return err
	}
	e.head = align(e.head, a)
	return e.marshalStructFields(t, v)
}

// marshalStructPointer marshals a nullable struct's reference, and then marshals
// the struct itself out-of-line.
//
// Expects the Type t and Value v to refer to a pointer to a struct.
func (e *encoder) marshalStructPointer(t reflect.Type, v reflect.Value) error {
	if v.IsNil() {
		e.writeUint(noAlloc, 8)
		return nil
	}
	e.writeUint(allocPresent, 8)
	et := t.Elem()
	ev := v.Elem()
	// Encode the struct out-of-line.
	s, err := getStructSize(et, ev)
	if err != nil {
		return err
	}
	oldHead := e.head
	e.head = e.newObject(align(s, 8))
	if err := e.marshalStructFields(et, ev); err != nil {
		return err
	}
	e.head = oldHead
	return nil
}

// marshalStructFields marshals the fields of a struct inline without any alignment.
//
// Expects the Type t and Value v to refer to a struct value, not a pointer.
//
// It marshals only exported struct fields.
func (e *encoder) marshalStructFields(t reflect.Type, v reflect.Value) error {
	for i := 0; i < t.NumField(); i++ {
		f := t.Field(i)
		// If it's an unexported field, ignore it.
		if f.PkgPath != "" {
			continue
		}
		var n nestedTypeData
		if err := n.FromTag(f.Tag); err != nil {
			return err
		}
		if err := e.marshal(f.Type, v.Field(i), n); err != nil {
			return err
		}
	}
	return nil
}

// marshalArray marshals a FIDL array inline.
func (e *encoder) marshalArray(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	elemType := t.Elem()
	for i := 0; i < t.Len(); i++ {
		if err := e.marshal(elemType, v.Index(i), n); err != nil {
			return err
		}
	}
	return nil
}

// marshalInline marshals a number of different supported FIDL types inline.
func (e *encoder) marshalInline(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	switch t.Kind() {
	case reflect.Array:
		return e.marshalArray(t, v, n)
	case reflect.Bool:
		i := uint64(0)
		if v.Bool() {
			i = 1
		}
		e.writeUint(i, 1)
	case reflect.Int8:
		e.writeInt(v.Int(), 1)
	case reflect.Int16:
		e.writeInt(v.Int(), 2)
	case reflect.Int32:
		e.writeInt(v.Int(), 4)
	case reflect.Int64:
		e.writeInt(v.Int(), 8)
	case reflect.Uint8:
		e.writeUint(v.Uint(), 1)
	case reflect.Uint16:
		e.writeUint(v.Uint(), 2)
	case reflect.Uint32:
		e.writeUint(v.Uint(), 4)
	case reflect.Uint64:
		e.writeUint(v.Uint(), 8)
	case reflect.Float32:
		e.writeUint(uint64(math.Float32bits(float32(v.Float()))), 4)
	case reflect.Float64:
		e.writeUint(math.Float64bits(v.Float()), 8)
	case reflect.Struct:
		return e.marshalStructInline(t, v)
	default:
		return fmt.Errorf("unsupported inline type kind %s for type %s", t.Kind(), t.Name())
	}
	return nil
}

// marshalVector writes the vector metadata inline and the elements of the vector in a new
// out-of-line object. Expects the value to be a regular string value, i.e. of type string.
func (e *encoder) marshalVector(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	if !v.IsValid() {
		e.writeUint(0, 8)
		e.writeUint(noAlloc, 8)
		return nil
	}
	max := n.Unnest()
	if max != nil && v.Len() > *max {
		return fmt.Errorf("vector exceeds maximum length of %d", *max)
	}
	e.writeUint(uint64(v.Len()), 8)
	e.writeUint(allocPresent, 8)
	// Don't bother creating the out-of-line struct if its length is 0.
	if v.Len() == 0 {
		return nil
	}
	elemType := t.Elem()
	elemSize, err := getSize(elemType, v.Index(0))
	if err != nil {
		return err
	}
	// Encode in the out-of-line object.
	oldHead := e.head
	e.head = e.newObject(v.Len() * elemSize)
	for i := 0; i < v.Len(); i++ {
		if err := e.marshal(elemType, v.Index(i), n); err != nil {
			return err
		}
	}
	e.head = oldHead
	return nil
}

// marshalString writes the string metadata inline and the bytes of the string in a new
// out-of-line object. Expects the value to be a regular string value, i.e. of type string.
func (e *encoder) marshalString(v reflect.Value, n nestedTypeData) error {
	if !v.IsValid() {
		e.writeUint(0, 8)
		e.writeUint(noAlloc, 8)
		return nil
	}
	s := v.String()
	max := n.Unnest()
	if max != nil && len(s) > *max {
		return fmt.Errorf("string of length %d exceeds maximum length %d", len(s), *max)
	}
	e.writeUint(uint64(len(s)), 8)
	e.writeUint(allocPresent, 8)

	// Create a new out-of-line object and write bytes of the string.
	head := e.newObject(len(s))
	for i := 0; i < len(s); i++ {
		e.buffer[head+i] = s[i]
	}
	return nil
}

// marshalHandle marshals a Fuchsia handle type, and ensures that the handle is
// valid if it is not nullable.
func (e *encoder) marshalHandle(v reflect.Value, n nestedTypeData) error {
	// The underlying type of all the handles is a uint32, so we're
	// safe calling Uint(). This will panic if that is no longer true.
	raw := zx.Handle(v.Uint())
	if raw == zx.HANDLE_INVALID {
		if !n.nullable {
			return fmt.Errorf("encountered null handle for non-nullable handle type")
		}
		e.writeUint(uint64(noHandle), 4)
		return nil
	}
	e.handles = append(e.handles, raw)
	e.writeUint(uint64(handlePresent), 4)
	return nil
}

// marshalPointer marshals nullable FIDL types that are represented by golang pointer
// indirections. The input type and value should be the dereference of the golang pointers,
// that is, if we're marshalling *string, we should get string.
func (e *encoder) marshalPointer(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	switch t.Elem().Kind() {
	case reflect.Slice:
		return e.marshalVector(t.Elem(), v.Elem(), n)
	case reflect.String:
		return e.marshalString(v.Elem(), n)
	case reflect.Struct:
		return e.marshalStructPointer(t, v)
	}
	return fmt.Errorf("unsupported nullable type kind %s for type %s", t.Kind(), t.Name())
}

// marshal is the central recursive function core to marshalling, and
// traverses the tree-like structure of the input type t. v represents
// the value associated with the type t.
func (e *encoder) marshal(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	switch t.Kind() {
	case reflect.Ptr:
		return e.marshalPointer(t, v, n)
	case reflect.Slice:
		return e.marshalVector(t, v, n)
	case reflect.String:
		return e.marshalString(v, n)
	}
	if isHandleType(t) {
		return e.marshalHandle(v, n)
	}
	if isInterfaceType(t) {
		// An interface is represented by a Proxy, whose first field is
		// a zx.Channel, and we can just marshal that.
		return e.marshalHandle(v.Field(0), n)
	}
	return e.marshalInline(t, v, n)
}

// MarshalHeader encodes the FIDL message header into the beginning of data.
func MarshalHeader(header *MessageHeader, data []byte) {
	// Clear the buffer so we can append to it.
	e := encoder{buffer: data[:0]}
	e.head = e.newObject(MessageHeaderSize)
	e.writeUint(uint64(header.Txid), 4)
	e.writeUint(uint64(header.Reserved), 4)
	e.writeUint(uint64(header.Flags), 4)
	e.writeUint(uint64(header.Ordinal), 4)
}

// Marshal the FIDL payload in s into data and handles.
//
// s must be a pointer to a struct, since the primary object in a FIDL message
// is always a struct.
//
// Marshal traverses the value s recursively, following nested type values via
// reflection in order to encode the FIDL struct.
func Marshal(s Payload, data []byte, handles []zx.Handle) (int, int, error) {
	// First, let's make sure we have the right type in s.
	t := reflect.TypeOf(s)
	if t.Kind() != reflect.Ptr {
		return 0, 0, errors.New("expected a pointer")
	}
	t = t.Elem()
	if t.Kind() != reflect.Struct {
		return 0, 0, errors.New("primary object must be a struct")
	}

	// Now, let's get the value of s, marshal the header into a starting
	// buffer, and then marshal the rest of the payload in s.
	v := reflect.ValueOf(s).Elem()
	e := encoder{buffer: data[:0], handles: handles[:0]}
	e.head = e.newObject(s.InlineSize())
	if err := e.marshalStructFields(t, v); err != nil {
		return 0, 0, err
	}
	return len(e.buffer), len(e.handles), nil
}

// decoder represents the decoding context that is necessary to maintain
// across recursive calls within the same FIDL object.
type decoder struct {
	// head is the index into buffer at which new data will be read from for the current
	// object. It must be updated before reading from a new out-of-line object, and then
	// fixed when that object is finished.
	head int

	// nextObject is the byte index of the next out-of-line object in buffer.
	nextObject int

	// buffer represents the buffer we're decoding from.
	buffer []byte

	// handles represents the input untyped handled we're decoding.
	handles []zx.Handle
}

// readInt reads a signed integer value of byte-width size from the buffer.
//
// Before it reads, however, it moves the head forward so as to be naturally
// aligned with the byte-width of the integer it is reading.
//
// size must be a power of 2 <= 8.
func (d *decoder) readInt(size int) int64 {
	return int64(d.readUint(size))
}

// readUint reads an unsigned integer value of byte-width size from the buffer.
//
// Before it reads, however, it moves the head forward so as to be naturally
// aligned with the byte-width of the integer it is reading.
//
// size must be a power of 2 <= 8.
func (d *decoder) readUint(size int) uint64 {
	d.head = align(d.head, size)
	var val uint64
	for i := d.head + size - 1; i >= d.head; i-- {
		val <<= 8
		val |= uint64(d.buffer[i])
	}
	d.head += size
	return val
}

// unmarshalStructInline unmarshals a struct inline based on Type t into Value v, first
// aligning head to the alignment of the struct.
//
// Expects the Type t and Value v to refer to a struct value, not a pointer.
func (d *decoder) unmarshalStructInline(t reflect.Type, v reflect.Value) error {
	a, err := getStructAlignment(t, v)
	if err != nil {
		return err
	}
	d.head = align(d.head, a)
	return d.unmarshalStructFields(t, v)
}

// unmarshalStructPointer unmarshals a pointer to a struct (i.e. a nullable FIDL struct)
// into the Value v.
//
// Expects the Type t and Value v to refer to a pointer to a struct.
func (d *decoder) unmarshalStructPointer(t reflect.Type, v reflect.Value) error {
	if d.readUint(8) == noAlloc {
		v.Set(reflect.Zero(t))
		return nil
	}
	// Create the new struct.
	v.Set(reflect.New(t.Elem()))
	et := t.Elem()
	ev := v.Elem()

	// Set up the out-of-line space and the head.
	oldHead := d.head
	d.head = d.nextObject
	s, err := getStructSize(et, ev)
	if err != nil {
		return err
	}
	d.nextObject += align(s, 8)

	// Marshal all the fields of the struct out-of-line.
	if err := d.unmarshalStructFields(et, ev); err != nil {
		return err
	}

	// Fix up head to the old head if it's a nullable struct.
	d.head = oldHead
	return nil
}

// unmarshalStructFields unmarshals the exported fields of the struct at d.head into
// the Value v.
//
// Expects the Type t and Value v to refer to a struct value, not a pointer.
func (d *decoder) unmarshalStructFields(t reflect.Type, v reflect.Value) error {
	for i := 0; i < t.NumField(); i++ {
		f := t.Field(i)
		// If it's an unexported field, ignore it.
		if f.PkgPath != "" {
			continue
		}
		var n nestedTypeData
		if err := n.FromTag(f.Tag); err != nil {
			return err
		}
		if err := d.unmarshal(f.Type, v.Field(i), n); err != nil {
			return err
		}
	}
	return nil
}

// unmarshalArray unmarshals an array inline based on Type t into Value v, taking into account
// nestedTypeData n, since an array is a container type.
func (d *decoder) unmarshalArray(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	elemType := t.Elem()
	for i := 0; i < t.Len(); i++ {
		if err := d.unmarshal(elemType, v.Index(i), n); err != nil {
			return err
		}
	}
	return nil
}

// unmarshalInline unmarshals a variety of types inline, or delegates to their types' specific
// unmarshalling functions.
func (d *decoder) unmarshalInline(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	switch t.Kind() {
	case reflect.Array:
		return d.unmarshalArray(t, v, n)
	case reflect.Bool:
		i := d.readUint(1)
		switch i {
		case 0:
			v.SetBool(false)
		case 1:
			v.SetBool(true)
		default:
			return fmt.Errorf("%d is not a valid bool value", i)
		}
	case reflect.Int8:
		v.SetInt(d.readInt(1))
	case reflect.Int16:
		v.SetInt(d.readInt(2))
	case reflect.Int32:
		v.SetInt(d.readInt(4))
	case reflect.Int64:
		v.SetInt(d.readInt(8))
	case reflect.Uint8:
		v.SetUint(d.readUint(1))
	case reflect.Uint16:
		v.SetUint(d.readUint(2))
	case reflect.Uint32:
		v.SetUint(d.readUint(4))
	case reflect.Uint64:
		v.SetUint(d.readUint(8))
	case reflect.Float32:
		v.SetFloat(float64(math.Float32frombits(uint32(d.readUint(4)))))
	case reflect.Float64:
		v.SetFloat(math.Float64frombits(d.readUint(8)))
	case reflect.Struct:
		return d.unmarshalStructInline(t, v)
	default:
		return fmt.Errorf("unsupported inline type kind %s for type %s", t.Kind(), t.Name())
	}
	return nil
}

// unmarshalVector unmarshals an out-of-line FIDL vector into a golang slice which is placed
// into v. nestedTypeData is also necessary because a vector can have a maximum size). The
// expected types and values are either pointers, i.e. *[]int8 or an "inline" vector value
// i.e. []int8.
func (d *decoder) unmarshalVector(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	size := int64(d.readUint(8))
	if size < 0 {
		return fmt.Errorf("vector length exceeds golang positive int")
	}
	if ptr := d.readUint(8); ptr == noAlloc {
		if t.Kind() != reflect.Ptr {
			return fmt.Errorf("unexpected null vector, vector is not nullable")
		}
		v.Set(reflect.Zero(t))
		return nil
	}
	max := n.Unnest()
	if max != nil && int(size) > *max {
		return fmt.Errorf("vector of length %d exceeds maximum length of %d", size, *max)
	}

	// Create the slice with reflection.
	sliceType := t
	elemType := t.Elem()
	if t.Kind() == reflect.Ptr {
		sliceType = elemType
		elemType = sliceType.Elem()
	}
	s := reflect.MakeSlice(sliceType, int(size), int(size))

	// Unmarshal the out-of-line structure.
	oldHead := d.head
	d.head = d.nextObject
	// TODO(mknyszek): Get rid of this extra reflect.New somehow.
	elemSize, err := getSize(elemType, reflect.New(elemType).Elem())
	if err != nil {
		return err
	}
	d.nextObject += align(int(size)*elemSize, 8)
	for i := 0; i < int(size); i++ {
		if err := d.unmarshal(elemType, s.Index(i), n); err != nil {
			return err
		}
	}
	d.head = oldHead
	if t.Kind() == reflect.Ptr {
		v.Set(reflect.New(t.Elem()))
		v.Elem().Set(s)
	} else {
		v.Set(s)
	}
	return nil
}

// unmarshalString unmarshals an out-of-line FIDL string into a golang string which is placed
// into v. nestedTypeData is also necessary because it can have a maximum size). The expected
// types and values are either pointers, i.e. *string or an "inline" string value. i.e. string.
// This method uses whether or not the type is a golang pointer type to determine if it is
// nullable.
func (d *decoder) unmarshalString(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	size := int64(d.readUint(8))
	if size < 0 {
		return fmt.Errorf("string length exceeds golang positive int")
	}
	if ptr := d.readUint(8); ptr == noAlloc {
		if t.Kind() != reflect.Ptr {
			return fmt.Errorf("unexpected null string, string is not nullable")
		}
		v.Set(reflect.Zero(t))
		return nil
	}
	max := n.Unnest()
	if max != nil && int(size) > *max {
		return fmt.Errorf("string of length %d exceeds maximum length of %d", size, *max)
	}
	s := string(d.buffer[d.nextObject : d.nextObject+int(size)])
	if t.Kind() == reflect.Ptr {
		v.Set(reflect.New(t.Elem()))
		v.Elem().Set(reflect.ValueOf(s))
	} else {
		v.Set(reflect.ValueOf(s))
	}
	d.nextObject += align(int(size), 8)
	return nil
}

// unmarshalHandle unmarshals a handle into a value, validating that it is valid if the handle is
// not nullable.
func (d *decoder) unmarshalHandle(v reflect.Value, n nestedTypeData) error {
	h := d.readUint(4)
	switch h {
	case uint64(noHandle):
		if !n.nullable {
			return fmt.Errorf("found invalid handle passed for non-nullable handle type")
		}
		v.SetUint(uint64(zx.HANDLE_INVALID))
	case uint64(handlePresent):
		if len(d.handles) == 0 {
			return fmt.Errorf("not enough handles available for decoding")
		}
		v.SetUint(uint64(d.handles[0]))
		d.handles = d.handles[1:]
	default:
		return fmt.Errorf("unsupported value %d for encoded handle", h)
	}
	return nil
}

// unmarshalPointer unmarshals nullable FIDL types that are represented by golang pointer
// indirections. The expected types and values t and v are pointers, i.e. they start with *.
func (d *decoder) unmarshalPointer(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	switch t.Elem().Kind() {
	case reflect.Slice:
		return d.unmarshalVector(t, v, n)
	case reflect.String:
		return d.unmarshalString(t, v, n)
	case reflect.Struct:
		return d.unmarshalStructPointer(t, v)
	}
	return fmt.Errorf("unsupported nullable type kind %s for type %s", t.Kind(), t.Name())
}

// unmarshal is the central recursive function core to unmarshalling, and
// traverses the tree-like structure of the input type t. v represents
// the value associated with the type t.
func (d *decoder) unmarshal(t reflect.Type, v reflect.Value, n nestedTypeData) error {
	switch t.Kind() {
	case reflect.Ptr:
		return d.unmarshalPointer(t, v, n)
	case reflect.Slice:
		return d.unmarshalVector(t, v, n)
	case reflect.String:
		return d.unmarshalString(t, v, n)
	}
	if isHandleType(t) {
		return d.unmarshalHandle(v, n)
	}
	if isInterfaceType(t) {
		// An interface is represented by a Proxy, whose first field is
		// a zx.Channel, and we can just unmarshal that.
		return d.unmarshalHandle(v.Field(0), n)
	}
	return d.unmarshalInline(t, v, n)
}

// UnmarshalHeader parses a FIDL header in the data into m.
func UnmarshalHeader(data []byte, m *MessageHeader) error {
	if len(data) < 16 {
		return fmt.Errorf("too few bytes in payload to parse header")
	}
	d := decoder{buffer: data}
	m.Txid = uint32(d.readUint(4))
	m.Reserved = uint32(d.readUint(4))
	m.Flags = uint32(d.readUint(4))
	m.Ordinal = uint32(d.readUint(4))
	return nil
}

// Unmarshal parses the encoded FIDL payload in data and handles, storing the
// decoded payload in s.
//
// The value pointed to by s must be a pointer to a golang struct which represents
// the decoded primary object of a FIDL message. The data decode process is guided
// by the structure of the struct pointed to by s.
//
// TODO(mknyszek): More rigorously validate the input.
func Unmarshal(data []byte, handles []zx.Handle, s Payload) error {
	// First, let's make sure we have the right type in s.
	t := reflect.TypeOf(s)
	if t.Kind() != reflect.Ptr {
		return errors.New("expected a pointer")
	}
	t = t.Elem()
	if t.Kind() != reflect.Struct {
		return errors.New("primary object must be a struct")
	}

	// Get the payload's value and unmarshal it.
	nextObject := align(s.InlineSize(), 8)
	d := decoder{
		buffer:     data,
		handles:    handles,
		nextObject: nextObject,
	}
	return d.unmarshalStructFields(t, reflect.ValueOf(s).Elem())
}
