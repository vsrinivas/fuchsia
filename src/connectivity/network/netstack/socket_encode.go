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

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

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

func (v *C.struct_sockaddr_storage) Decode() (tcpip.FullAddress, error) {
	switch v.ss_family {
	case C.AF_INET:
		v := (*C.struct_sockaddr_in)(unsafe.Pointer(v))
		out := tcpip.FullAddress{
			Port: binary.BigEndian.Uint16(v.sin_port.Bytes()),
		}
		if b := v.sin_addr.Bytes(); !isZeros(b) {
			out.Addr = tcpip.Address(b)
		}
		return out, nil
	case C.AF_INET6:
		v := (*C.struct_sockaddr_in6)(unsafe.Pointer(v))
		out := tcpip.FullAddress{
			Port: binary.BigEndian.Uint16(v.sin6_port.Bytes()),
		}
		if b := v.sin6_addr.Bytes(); !isZeros(b) {
			out.Addr = tcpip.Address(b)
		}
		if isLinkLocal(out.Addr) {
			out.NIC = tcpip.NICID(v.sin6_scope_id)
		}
		return out, nil
	case C.AF_UNSPEC:
		return tcpip.FullAddress{}, nil
	default:
		return tcpip.FullAddress{}, fmt.Errorf("unknown sockaddr_storage.ss_family: %d", v.ss_family)
	}
}

func (v *C.struct_sockaddr_storage) Encode(netProto tcpip.NetworkProtocolNumber, addr tcpip.FullAddress) int {
	switch netProto {
	case ipv4.ProtocolNumber:
		v := (*C.struct_sockaddr_in)(unsafe.Pointer(v))
		copy(v.sin_addr.Bytes(), addr.Addr)
		v.sin_family = C.AF_INET
		binary.BigEndian.PutUint16(v.sin_port.Bytes(), addr.Port)
		return C.sizeof_struct_sockaddr_in
	case ipv6.ProtocolNumber:
		v := (*C.struct_sockaddr_in6)(unsafe.Pointer(v))
		if len(addr.Addr) == header.IPv4AddressSize {
			// Copy address in v4-mapped format.
			copy(v.sin6_addr.Bytes()[header.IPv6AddressSize-header.IPv4AddressSize:], addr.Addr)
			v.sin6_addr.Bytes()[header.IPv6AddressSize-header.IPv4AddressSize-1] = 0xff
			v.sin6_addr.Bytes()[header.IPv6AddressSize-header.IPv4AddressSize-2] = 0xff
		} else {
			copy(v.sin6_addr.Bytes(), addr.Addr)
		}
		v.sin6_family = C.AF_INET6
		binary.BigEndian.PutUint16(v.sin6_port.Bytes(), addr.Port)
		if isLinkLocal(addr.Addr) {
			v.sin6_scope_id = C.uint32_t(addr.NIC)
		}
		return C.sizeof_struct_sockaddr_in6
	default:
		panic(fmt.Sprintf("unknown network protocol number: %v", netProto))
	}
}

func encodeAddr(netProto tcpip.NetworkProtocolNumber, addr tcpip.FullAddress) []uint8 {
	var v C.struct_sockaddr_storage
	n := v.Encode(netProto, addr)
	return (*[C.sizeof_struct_sockaddr_storage]byte)(unsafe.Pointer(&v))[:n]
}
