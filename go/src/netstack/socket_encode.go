// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ad-hoc unsafe encoding/decoding of C data types used by the
// protocol libc talks to netstack.

package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"reflect"
	"syscall/zx"
	"syscall/zx/mxerror"
	"syscall/zx/zxsocket"
	"unsafe"

	"github.com/google/netstack/tcpip"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #cgo CFLAGS: -I${SRCDIR}/../../../public
// #include <lib/zxs/protocol.h>
// #include <netinet/tcp.h>
// #include <lib/netstack/c/netconfig.h>
import "C"

func (v *C.struct_zxrio_sockopt_req_reply) optBytes() []byte {
	b := v.optval[:v.optlen]
	return *(*[]byte)(unsafe.Pointer(&b))
}

func (v *C.struct_zxrio_sockopt_req_reply) Decode(msg *zxsocket.Msg) error {
	if msg.Datalen < C.sizeof_struct_zxrio_sockopt_req_reply {
		return fmt.Errorf("netstack: short C.struct_zxrio_sockopt_req_reply: %d", msg.Datalen)
	}
	data := msg.Data[:msg.Datalen]
	r := (*C.struct_zxrio_sockopt_req_reply)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	*v = *r
	return nil
}

func (v *C.struct_zxrio_sockopt_req_reply) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[C.sizeof_struct_zxrio_sockopt_req_reply]byte)(unsafe.Pointer(v))[:]))
}

func (v *C.struct_tcp_info) Encode(out *C.struct_zxrio_sockopt_req_reply) {
	out.optlen = C.uint(len(out.optval))
	out.optlen = C.socklen_t(copy(out.optBytes(), (*[C.sizeof_struct_tcp_info]byte)(unsafe.Pointer(v))[:]))
	// TODO(tamird): why are we encoding 144 bytes into a 128 byte buffer?
	out.optlen += 16
}

func (v *C.struct_zxrio_sockaddr_reply) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[C.sizeof_struct_zxrio_sockaddr_reply]byte)(unsafe.Pointer(v))[:]))
}

func (v *C.struct_ip_mreq) Decode(data []byte) error {
	if uintptr(len(data)) < C.sizeof_struct_ip_mreq {
		return fmt.Errorf("netstack: short C.struct_ip_mreq: %d", len(data))
	}
	r := (*C.struct_ip_mreq)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	*v = *r
	return nil
}

func (v *C.struct_ip_mreqn) Decode(data []byte) error {
	if len(data) < C.sizeof_struct_ip_mreqn {
		return fmt.Errorf("netstack: short C.struct_ip_mreqn: %d", len(data))
	}
	r := (*C.struct_ip_mreqn)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	*v = *r
	return nil
}

func (v *C.netc_get_if_info_t) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[C.sizeof_netc_get_if_info_t]byte)(unsafe.Pointer(v))[:]))
}

func (v *C.netc_if_info_t) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[C.sizeof_netc_if_info_t]byte)(unsafe.Pointer(v))[:]))
}

func (v *C.struct_in_addr) Bytes() []byte {
	return (*(*[C.sizeof_struct_in_addr]byte)(unsafe.Pointer(v)))[:]
}

func (v *C.struct_in6_addr) Bytes() []byte {
	return (*(*[C.sizeof_struct_in6_addr]byte)(unsafe.Pointer(v)))[:]
}

func (v *C.in_port_t) Bytes() []byte {
	return (*(*[C.sizeof_in_port_t]byte)(unsafe.Pointer(v)))[:]
}

// TODO: make these methods on C.struct_sockaddr_storage
func writeSockaddrStorage4(dst *C.struct_sockaddr_storage, src *C.struct_sockaddr_in) C.socklen_t {
	srcb := (*[C.sizeof_struct_sockaddr_in]byte)(unsafe.Pointer(src))[:]
	dstb := (*[C.sizeof_struct_sockaddr_storage]byte)(unsafe.Pointer(dst))[:]
	return C.socklen_t(copy(dstb, srcb))
}
func writeSockaddrStorage6(dst *C.struct_sockaddr_storage, src *C.struct_sockaddr_in6) C.socklen_t {
	srcb := (*[C.sizeof_struct_sockaddr_in6]byte)(unsafe.Pointer(src))[:]
	dstb := (*[C.sizeof_struct_sockaddr_storage]byte)(unsafe.Pointer(dst))[:]
	return C.socklen_t(copy(dstb, srcb))
}
func writeSockaddrStorage(dst *C.struct_sockaddr_storage, a tcpip.FullAddress) (C.socklen_t, error) {
	switch len(a.Addr) {
	case 0, 4:
		sockaddr := C.struct_sockaddr_in{sin_family: C.AF_INET}
		binary.BigEndian.PutUint16(sockaddr.sin_port.Bytes(), a.Port)
		copy(sockaddr.sin_addr.Bytes(), a.Addr)
		return writeSockaddrStorage4(dst, &sockaddr), nil
	case 16:
		sockaddr := C.struct_sockaddr_in6{sin6_family: C.AF_INET6}
		binary.BigEndian.PutUint16(sockaddr.sin6_port.Bytes(), a.Port)
		copy(sockaddr.sin6_addr.Bytes(), a.Addr)
		return writeSockaddrStorage6(dst, &sockaddr), nil
	}
	return 0, mxerror.Errorf(zx.ErrInvalidArgs, "write sockaddr: bad address len %d", len(a.Addr))
}

func readSockaddrIn(data []byte) (tcpip.FullAddress, error) {
	// TODO: recast in terms of C.struct_sockaddr_storage
	// TODO: split out the not-unsafe parts into socket_conv.go.
	family := *(*uint16)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	if debug {
		log.Printf("readSockaddrIn: family=%d", family)
	}
	switch family {
	case C.AF_INET:
		if len(data) < C.sizeof_struct_sockaddr_in {
			return tcpip.FullAddress{}, mxerror.Errorf(zx.ErrInvalidArgs, "reading C.struct_sockaddr_in: len(data)=%d too small", len(data))
		}
		v := (*C.struct_sockaddr_in)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
		addr := tcpip.FullAddress{
			Port: binary.BigEndian.Uint16(v.sin_port.Bytes()),
		}
		// INADDR_ANY is represented as tcpip.Address("").
		if sin_addr := v.sin_addr.Bytes(); !isZeros(sin_addr) {
			addr.Addr = tcpip.Address(sin_addr)
		}
		if debug {
			log.Printf("readSockaddrIn: addr=%v", addr)
		}
		return addr, nil
	case C.AF_INET6:
		if len(data) < C.sizeof_struct_sockaddr_in6 {
			return tcpip.FullAddress{}, mxerror.Errorf(zx.ErrInvalidArgs, "reading C.struct_sockaddr_in6: len(data)=%d too small", len(data))
		}
		v := (*C.struct_sockaddr_in6)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
		addr := tcpip.FullAddress{
			Port: binary.BigEndian.Uint16(v.sin6_port.Bytes()),
		}
		if sin6_addr := v.sin6_addr.Bytes(); !isZeros(sin6_addr) {
			addr.Addr = tcpip.Address(sin6_addr)
		}
		if debug {
			log.Printf("readSockaddrIn: addr=%v", addr)
		}
		return addr, nil
	default:
		return tcpip.FullAddress{}, mxerror.Errorf(zx.ErrInvalidArgs, "reading C.struct_sockaddr: unknown family: %d", family)
	}
}

func readSocketMsgHdr(data []byte) (*tcpip.FullAddress, error) {
	if len(data) < C.FDIO_SOCKET_MSG_HEADER_SIZE {
		return nil, mxerror.Errorf(zx.ErrInvalidArgs, "reading socket msg header: too short: %d", len(data))
	}
	hdr := (*C.struct_fdio_socket_msg)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	if hdr.addrlen == 0 {
		return nil, nil
	}
	addr, err := readSockaddrIn(data) // first field of C.struct_fdio_socket_msg is C.struct_sockaddr_storage
	if err != nil {
		return nil, err
	}
	return &addr, nil
}

func writeSocketMsgHdr(data []byte, addr tcpip.FullAddress) error {
	if len(data) < C.FDIO_SOCKET_MSG_HEADER_SIZE {
		return mxerror.Errorf(zx.ErrInvalidArgs, "writing socket msg header: too short: %d", len(data))
	}
	hdr := (*C.struct_fdio_socket_msg)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	l, err := writeSockaddrStorage(&hdr.addr, addr)
	hdr.addrlen = l
	hdr.flags = 0
	return err
}
