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

import "C"

const (
	c_sockaddr_in_len          = int(unsafe.Sizeof(c_sockaddr_in{}))
	c_sockaddr_in6_len         = int(unsafe.Sizeof(c_sockaddr_in6{}))
	c_sockaddr_storage_len     = int(unsafe.Sizeof(c_sockaddr_storage{}))
	c_mxrio_sockaddr_reply_len = int(unsafe.Sizeof(c_mxrio_sockaddr_reply{}))
	c_fdio_socket_msg_hdr_len  = int(unsafe.Sizeof(c_fdio_socket_msg_hdr{}))
)

func init() {
	if c_sockaddr_storage_len < c_sockaddr_in_len {
		panic("c_sockaddr_storage cannot hold c_sockaddr_in")
	}
	if c_sockaddr_storage_len < c_sockaddr_in6_len {
		panic("c_sockaddr_storage cannot hold c_sockaddr_in6")
	}
}

func (v *c_mxrio_sockopt_req_reply) Decode(msg *zxsocket.Msg) error {
	if msg.Datalen < uint32(unsafe.Sizeof(c_mxrio_sockopt_req_reply{})) {
		return fmt.Errorf("netstack: short c_mxrio_sockopt_req_reply: %d", msg.Datalen)
	}
	data := msg.Data[:msg.Datalen]
	r := (*c_mxrio_sockopt_req_reply)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	*v = *r
	return nil
}

func (v *c_mxrio_sockopt_req_reply) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[unsafe.Sizeof(c_mxrio_sockopt_req_reply{})]byte)(unsafe.Pointer(v))[:]))
}

func (v *c_mxrio_sockopt_tcp_info) Encode(out *c_mxrio_sockopt_req_reply) {
	out.optlen = c_socklen(copy(out.optval[:], (*[unsafe.Sizeof(c_mxrio_sockopt_tcp_info{})]byte)(unsafe.Pointer(v))[:]))
	// TODO(tamird): why are we encoding 144 bytes into a 128 byte buffer?
	out.optlen += 16
}

func (v *c_mxrio_sockaddr_reply) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[unsafe.Sizeof(c_mxrio_sockaddr_reply{})]byte)(unsafe.Pointer(v))[:]))
}

func (v *c_ip_mreq) Decode(data []byte) error {
	if uintptr(len(data)) < unsafe.Sizeof(c_ip_mreq{}) {
		return fmt.Errorf("netstack: short c_ip_mreq: %d", len(data))
	}
	r := (*c_ip_mreq)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	*v = *r
	return nil
}

func (v *c_ip_mreqn) Decode(data []byte) error {
	if uintptr(len(data)) < unsafe.Sizeof(c_ip_mreqn{}) {
		return fmt.Errorf("netstack: short c_ip_mreqn: %d", len(data))
	}
	r := (*c_ip_mreqn)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	*v = *r
	return nil
}

func (v *c_netc_get_if_info) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[unsafe.Sizeof(c_netc_get_if_info{})]byte)(unsafe.Pointer(v))[:]))
}

func (v *c_netc_if_info) Encode(msg *zxsocket.Msg) {
	msg.Datalen = uint32(copy(msg.Data[:], (*[unsafe.Sizeof(c_netc_if_info{})]byte)(unsafe.Pointer(v))[:]))
}

// TODO: make these methods on c_sockaddr_storage
func writeSockaddrStorage4(dst *c_sockaddr_storage, src *c_sockaddr_in) c_socklen {
	srcb := (*[unsafe.Sizeof(*src)]byte)(unsafe.Pointer(src))[:]
	dstb := (*[unsafe.Sizeof(*dst)]byte)(unsafe.Pointer(dst))[:]
	return c_socklen(copy(dstb, srcb))
}
func writeSockaddrStorage6(dst *c_sockaddr_storage, src *c_sockaddr_in6) c_socklen {
	srcb := (*[unsafe.Sizeof(*src)]byte)(unsafe.Pointer(src))[:]
	dstb := (*[unsafe.Sizeof(*dst)]byte)(unsafe.Pointer(dst))[:]
	return c_socklen(copy(dstb, srcb))
}
func writeSockaddrStorage(dst *c_sockaddr_storage, a tcpip.FullAddress) (c_socklen, error) {
	switch len(a.Addr) {
	case 0, 4:
		sockaddr := c_sockaddr_in{sin_family: AF_INET}
		binary.BigEndian.PutUint16(sockaddr.sin_port[:], a.Port)
		copy(sockaddr.sin_addr[:], a.Addr)
		return writeSockaddrStorage4(dst, &sockaddr), nil
	case 16:
		sockaddr := c_sockaddr_in6{sin6_family: AF_INET6}
		binary.BigEndian.PutUint16(sockaddr.sin6_port[:], a.Port)
		copy(sockaddr.sin6_addr[:], a.Addr)
		return writeSockaddrStorage6(dst, &sockaddr), nil
	}
	return 0, mxerror.Errorf(zx.ErrInvalidArgs, "write sockaddr: bad address len %d", len(a.Addr))
}

func readSockaddrIn(data []byte) (tcpip.FullAddress, error) {
	// TODO: recast in terms of c_sockaddr_storage
	// TODO: split out the not-unsafe parts into socket_conv.go.
	family := *(*uint16)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	if debug {
		log.Printf("readSockaddrIn: family=%d", family)
	}
	switch family {
	case AF_INET:
		if len(data) < int(unsafe.Sizeof(c_sockaddr_in{})) {
			return tcpip.FullAddress{}, mxerror.Errorf(zx.ErrInvalidArgs, "reading c_sockaddr_in: len(data)=%d too small", len(data))
		}
		v := (*c_sockaddr_in)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
		addr := tcpip.FullAddress{
			Port: binary.BigEndian.Uint16(v.sin_port[:]),
		}
		// INADDR_ANY is represented as tcpip.Address("").
		if !isZeros(v.sin_addr[:]) {
			addr.Addr = tcpip.Address(v.sin_addr[:])
		}
		if debug {
			log.Printf("readSockaddrIn: addr=%v", addr)
		}
		return addr, nil
	case AF_INET6:
		if len(data) < int(unsafe.Sizeof(c_sockaddr_in6{})) {
			return tcpip.FullAddress{}, mxerror.Errorf(zx.ErrInvalidArgs, "reading c_sockaddr_in6: len(data)=%d too small", len(data))
		}
		v := (*c_sockaddr_in6)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
		addr := tcpip.FullAddress{
			Port: binary.BigEndian.Uint16(v.sin6_port[:]),
		}
		if !isZeros(v.sin6_addr[:]) {
			addr.Addr = tcpip.Address(v.sin6_addr[:])
		}
		if debug {
			log.Printf("readSockaddrIn: addr=%v", addr)
		}
		return addr, nil
	default:
		return tcpip.FullAddress{}, mxerror.Errorf(zx.ErrInvalidArgs, "reading c_sockaddr: unknown family: %d", family)
	}
}

func readSocketMsgHdr(data []byte) (*tcpip.FullAddress, error) {
	if len(data) < c_fdio_socket_msg_hdr_len {
		return nil, mxerror.Errorf(zx.ErrInvalidArgs, "reading socket msg header: too short: %d", len(data))
	}
	hdr := (*c_fdio_socket_msg_hdr)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	if hdr.addrlen == 0 {
		return nil, nil
	}
	addr, err := readSockaddrIn(data) // first field of c_fdio_socket_msg_hdr is c_sockaddr_storage
	if err != nil {
		return nil, err
	}
	return &addr, nil
}

func writeSocketMsgHdr(data []byte, addr tcpip.FullAddress) error {
	if len(data) < c_fdio_socket_msg_hdr_len {
		return mxerror.Errorf(zx.ErrInvalidArgs, "writing socket msg header: too short: %d", len(data))
	}
	hdr := (*c_fdio_socket_msg_hdr)(unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&data)).Data))
	l, err := writeSockaddrStorage(&hdr.addr, addr)
	hdr.addrlen = l
	hdr.flags = 0
	return err
}
