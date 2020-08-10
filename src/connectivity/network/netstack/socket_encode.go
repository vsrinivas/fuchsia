// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ad-hoc unsafe encoding/decoding of C data types used by the
// protocol libc talks to netstack.

package netstack

import (
	"encoding/binary"
	"fmt"
	"reflect"
	"unsafe"

	fidlnet "fidl/fuchsia/net"
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

func (v *C.in_port_t) Bytes() []byte {
	return (*[C.sizeof_in_port_t]byte)(unsafe.Pointer(v))[:]
}

func (v *C.struct_sockaddr_storage) Unmarshal(data []byte) error {
	const size = C.sizeof_struct_sockaddr_storage

	n := copy((*[size]byte)(unsafe.Pointer(v))[:], data)
	switch v.ss_family {
	case C.AF_INET:
		if size := C.sizeof_struct_sockaddr_in; n < size {
			return fmt.Errorf("short %T: %d/%d", v, n, size)
		}
	case C.AF_INET6:
		if size := C.sizeof_struct_sockaddr_in6; n < size {
			return fmt.Errorf("short %T: %d/%d", v, n, size)
		}
	case C.AF_UNSPEC:
	default:
		return fmt.Errorf("unknown sockaddr_storage.ss_family: %d", v.ss_family)
	}
	return nil
}

func isZeros(b []byte) bool {
	for _, b := range b {
		if b != 0 {
			return false
		}
	}
	return true
}

// Decode decodes the struct to fuchsia.net/SocketAddress and whether the it is
// AF_UNSPEC.
func (v *C.struct_sockaddr_storage) Decode() (fidlnet.SocketAddress, bool, error) {
	switch v.ss_family {
	case C.AF_INET:
		v := (*C.struct_sockaddr_in)(unsafe.Pointer(v))

		out := fidlnet.SocketAddressWithIpv4(fidlnet.Ipv4SocketAddress{
			Port: binary.BigEndian.Uint16(v.sin_port.Bytes()),
		})
		copy(out.Ipv4.Address.Addr[:], v.sin_addr.Bytes())
		return out, false, nil
	case C.AF_INET6:
		v := (*C.struct_sockaddr_in6)(unsafe.Pointer(v))
		out := fidlnet.SocketAddressWithIpv6(fidlnet.Ipv6SocketAddress{
			Port: binary.BigEndian.Uint16(v.sin6_port.Bytes()),
		})
		copy(out.Ipv6.Address.Addr[:], v.sin6_addr.Bytes())
		if isLinkLocal(out.Ipv6.Address) {
			out.Ipv6.ZoneIndex = uint64(v.sin6_scope_id)
		}
		return out, false, nil
	case C.AF_UNSPEC:
		return fidlnet.SocketAddress{}, true, nil
	default:
		return fidlnet.SocketAddress{}, false, fmt.Errorf("unknown sockaddr_storage.ss_family: %d", v.ss_family)
	}
}

// Encode encodes a FIDL socket address into this sockaddr struct and returns
// the length of the encoded address in bytes.
func (v *C.struct_sockaddr_storage) Encode(address fidlnet.SocketAddress) int {
	switch address.Which() {
	case fidlnet.SocketAddressIpv4:
		v := (*C.struct_sockaddr_in)(unsafe.Pointer(v))
		copy(v.sin_addr.Bytes(), address.Ipv4.Address.Addr[:])
		v.sin_family = C.AF_INET
		binary.BigEndian.PutUint16(v.sin_port.Bytes(), address.Ipv4.Port)
		return C.sizeof_struct_sockaddr_in
	case fidlnet.SocketAddressIpv6:
		v := (*C.struct_sockaddr_in6)(unsafe.Pointer(v))

		copy(v.sin6_addr.Bytes(), address.Ipv6.Address.Addr[:])
		v.sin6_family = C.AF_INET6
		binary.BigEndian.PutUint16(v.sin6_port.Bytes(), address.Ipv6.Port)
		if isLinkLocal(address.Ipv6.Address) {
			v.sin6_scope_id = C.uint32_t(address.Ipv6.ZoneIndex)
		}
		return C.sizeof_struct_sockaddr_in6
	default:
		panic(fmt.Sprintf("unknown network fuchsia.net/SocketAddress variant: %d", address.Which()))
	}
}

// decodeAddr decodes a sockaddr struct to fuchsia.net/SocketAddress and whether
// the sockaddr is AF_UNSPEC.
func decodeAddr(addr []uint8) (fidlnet.SocketAddress, bool, error) {
	var sockaddrStorage C.struct_sockaddr_storage
	if err := sockaddrStorage.Unmarshal(addr); err != nil {
		return fidlnet.SocketAddress{}, false, err
	}
	return sockaddrStorage.Decode()
}

// encodeAddr encodes a fuchsia.net/SocketAddress into a socketaddr struct.
func encodeAddr(address fidlnet.SocketAddress) []uint8 {
	var v C.struct_sockaddr_storage
	n := v.Encode(address)
	return (*[C.sizeof_struct_sockaddr_storage]byte)(unsafe.Pointer(&v))[:n]
}
