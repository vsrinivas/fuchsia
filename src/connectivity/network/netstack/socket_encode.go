// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ad-hoc unsafe encoding/decoding of C data types used by the
// protocol libc talks to netstack.

package netstack

import (
	"fmt"
	"reflect"
	"unsafe"
)

// TODO(fxbug.dev/44347) We shouldn't need any of this includes after we remove
// C structs from the wire.

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #include <netinet/in.h>
import "C"

// copyAsBytes exists because of a combination of issues:
//
// 1: cgo omits bitfields of sizes that don't have a corresponding Go type.
// Note that padding is still consistent with C, which is why reflect.Type.Size
// produces the correct result.
//
// 2: encoding/binary.{Size,Write} ignores padding.
//
// Therefore, since we know our endianness is the same as the decoder's, we can
// do a terrible thing and just interpret the struct as a byte array.
func copyAsBytes(b []byte, val interface{}) int {
	l := int(reflect.TypeOf(val).Size())
	v := reflect.Indirect(reflect.ValueOf(&val))
	return copy(b, *(*[]byte)(unsafe.Pointer(&reflect.SliceHeader{
		Data: v.InterfaceData()[1],
		Len:  l,
		Cap:  l,
	})))
}

func (v *C.struct_linger) Unmarshal(data []byte) error {
	const size = C.sizeof_struct_linger

	if n := copy((*[size]byte)(unsafe.Pointer(v))[:], data); n < size {
		return fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return nil
}

func (v *C.struct_ip_mreq) Unmarshal(data []byte) error {
	const size = C.sizeof_struct_ip_mreq

	if n := copy((*[size]byte)(unsafe.Pointer(v))[:], data); n < size {
		return fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return nil
}

func (v *C.struct_ip_mreqn) Unmarshal(data []byte) error {
	const size = C.sizeof_struct_ip_mreqn

	if n := copy((*[size]byte)(unsafe.Pointer(v))[:], data); n < size {
		return fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return nil
}

func (v *C.struct_ipv6_mreq) Unmarshal(data []byte) error {
	const size = C.sizeof_struct_ipv6_mreq

	if n := copy((*[size]byte)(unsafe.Pointer(v))[:], data); n < size {
		return fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return nil
}

func (v *C.struct_in_addr) Bytes() []byte {
	return (*[C.sizeof_struct_in_addr]byte)(unsafe.Pointer(v))[:]
}

func (v *C.struct_in6_addr) Bytes() []byte {
	return (*[C.sizeof_struct_in6_addr]byte)(unsafe.Pointer(v))[:]
}

func isZeros(b []byte) bool {
	for _, b := range b {
		if b != 0 {
			return false
		}
	}
	return true
}
