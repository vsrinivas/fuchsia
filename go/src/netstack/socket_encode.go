// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ad-hoc unsafe encoding/decoding of C data types used by the
// protocol libc talks to netstack.

package main

import (
	"fmt"
	"unsafe"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #cgo CFLAGS: -I${SRCDIR}/../../../public
// #include <lib/zxs/protocol.h>
// #include <netinet/tcp.h>
// #include <lib/netstack/c/netconfig.h>
import "C"

func (v *C.struct_fdio_socket_msg) Unmarshal(data []byte) error {
	const size = C.FDIO_SOCKET_MSG_HEADER_SIZE

	if n := copy((*[size]byte)(unsafe.Pointer(v))[:], data); n < size {
		return fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return nil
}

func (v *C.struct_fdio_socket_msg) MarshalTo(data []byte) (int, error) {
	const size = C.FDIO_SOCKET_MSG_HEADER_SIZE

	n := copy(data, (*[size]byte)(unsafe.Pointer(v))[:])
	if n < size {
		return 0, fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return n, nil
}

func (v *C.struct_zxrio_sockopt_req_reply) opt() []byte {
	b := v.optval[:]
	return *(*[]byte)(unsafe.Pointer(&b))
}

func (v *C.struct_zxrio_sockopt_req_reply) Unmarshal(data []byte) error {
	const size = C.sizeof_struct_zxrio_sockopt_req_reply

	if n := copy((*[size]byte)(unsafe.Pointer(v))[:], data); n < size {
		return fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return nil
}

func (v *C.struct_zxrio_sockopt_req_reply) MarshalTo(data []byte) (int, error) {
	const size = C.sizeof_struct_zxrio_sockopt_req_reply

	n := copy(data, (*[size]byte)(unsafe.Pointer(v))[:])
	if n < size {
		return 0, fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return n, nil
}

func (v *C.struct_tcp_info) MarshalTo(data []byte) (int, error) {
	const size = C.sizeof_struct_tcp_info

	n := copy(data, (*[size]byte)(unsafe.Pointer(v))[:])
	// TODO(tamird): why are we encoding 144 bytes into a 128 byte buffer?
	if n < size {
		n += 16
	}
	if n < size {
		return 0, fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return n, nil
}

func (v *C.struct_zxrio_sockaddr_reply) MarshalTo(data []byte) (int, error) {
	const size = C.sizeof_struct_zxrio_sockaddr_reply

	n := copy(data, (*[size]byte)(unsafe.Pointer(v))[:])
	if n < size {
		return 0, fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return n, nil
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

func (v *C.netc_get_if_info_t) MarshalTo(data []byte) (int, error) {
	const size = C.sizeof_netc_get_if_info_t

	n := copy(data, (*[size]byte)(unsafe.Pointer(v))[:])
	if n < size {
		return 0, fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return n, nil
}

func (v *C.netc_if_info_t) MarshalTo(data []byte) (int, error) {
	const size = C.sizeof_netc_if_info_t

	n := copy(data, (*[size]byte)(unsafe.Pointer(v))[:])
	if n < size {
		return 0, fmt.Errorf("short %T: %d/%d", v, n, size)
	}
	return n, nil
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
	default:
		return fmt.Errorf("unknown sockaddr_storage.ss_family: %d", v.ss_family)
	}
	return nil
}

func (v *C.struct_sockaddr_storage) Decode() (tcpip.FullAddress, error) {
	switch v.ss_family {
	case C.AF_INET:
		return (*C.struct_sockaddr_in)(unsafe.Pointer(v)).Decode(), nil
	case C.AF_INET6:
		return (*C.struct_sockaddr_in6)(unsafe.Pointer(v)).Decode(), nil
	default:
		return tcpip.FullAddress{}, fmt.Errorf("unknown sockaddr_storage.ss_family: %d", v.ss_family)
	}
}

func (v *C.struct_sockaddr_storage) Encode(addr tcpip.FullAddress) (int, error) {
	switch len(addr.Addr) {
	case header.IPv4AddressSize:
		return C.sizeof_struct_sockaddr_in, (*C.struct_sockaddr_in)(unsafe.Pointer(v)).Encode(addr)
	case header.IPv6AddressSize:
		return C.sizeof_struct_sockaddr_in6, (*C.struct_sockaddr_in6)(unsafe.Pointer(v)).Encode(addr)
	default:
		return 0, fmt.Errorf("unknown address family %+v", addr)
	}
}
