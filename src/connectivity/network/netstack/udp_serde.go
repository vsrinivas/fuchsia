// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	fidlnet "fidl/fuchsia/net"
	"fmt"
	"math"
	"reflect"
	"unsafe"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

// #cgo LDFLAGS: -ludp_serde_for_cgo -lc++ -lzircon
// #include <ifaddrs.h>
// #include <string.h>
// #include "udp_serde.h"
import "C"

func txUdpPreludeSize() uint32 {
	return uint32(C.kTxUdpPreludeSize)
}

func rxUdpPreludeSize() uint32 {
	return uint32(C.kRxUdpPreludeSize)
}

func deserializeSendMsgMetaErrToString(err C.DeserializeSendMsgMetaError) string {
	switch err {
	case C.DeserializeSendMsgMetaErrorNone:
		return "DeserializeSendMsgMetaErrorNone"
	case C.DeserializeSendMsgMetaErrorInputBufferNull:
		return "DeserializeSendMsgMetaErrorInputBufferNull"
	case C.DeserializeSendMsgMetaErrorInputBufferTooSmall:
		return "DeserializeSendMsgMetaErrorInputBufferTooSmall"
	case C.DeserializeSendMsgMetaErrorNonZeroPrelude:
		return "DeserializeSendMsgMetaErrorNonZeroPrelude"
	case C.DeserializeSendMsgMetaErrorFailedToDecode:
		return "DeserializeSendMsgMetaErrorFailedToDecode"
	default:
		panic(fmt.Sprintf("unknown deserialization result %#v", err))
	}
}

// deserializeSendMsgAddress deserializes metadata contained within `buf`
// as a SendMsgMeta FIDL message using the LLCPP bindings.
//
// If the deserialized metadata contains an address, returns that address.
// Else, returns nil.
func deserializeSendMsgAddress(buf []byte) *tcpip.FullAddress {
	bufIn := C.Buffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&buf))).Data)),
		buf_size: C.ulong(len(buf)),
	}

	res := C.deserialize_send_msg_meta(bufIn)
	if res.err != C.DeserializeSendMsgMetaErrorNone {
		panic(fmt.Sprintf("deserialization error: %s", deserializeSendMsgMetaErrToString(res.err)))
	}

	if res.has_addr {
		var addr tcpip.Address
		switch res.to_addr.addr_type {
		case C.Ipv4:
			if res.to_addr.addr_size != header.IPv4AddressSize {
				panic(fmt.Sprintf("expect IPv4 address has size %d, found size %d", header.IPv4AddressSize, res.to_addr.addr_size))
			}
			foundAddr := (*[header.IPv4AddressSize]uint8)(unsafe.Pointer(&res.to_addr.addr))
			addr = tcpip.Address(foundAddr[:])
		case C.Ipv6:
			if res.to_addr.addr_size != header.IPv6AddressSize {
				panic(fmt.Sprintf("expect IPv6 address has size %d, found size %d", header.IPv6AddressSize, res.to_addr.addr_size))
			}
			foundAddr := (*[header.IPv6AddressSize]uint8)(unsafe.Pointer(&res.to_addr.addr))
			addr = tcpip.Address(foundAddr[:])
		}
		return &tcpip.FullAddress{
			Addr: addr,
			Port: uint16(res.port),
		}
	}

	return nil
}

func serializeRecvMsgMetaErrorToString(err C.SerializeRecvMsgMetaError) string {
	switch err {
	case C.SerializeRecvMsgMetaErrorNone:
		return "SerializeRecvMsgMetaErrorNone"
	case C.SerializeRecvMsgMetaErrorOutputBufferNull:
		return "SerializeRecvMsgMetaErrorOutputBufferNull"
	case C.SerializeRecvMsgMetaErrorOutputBufferTooSmall:
		return "SerializeRecvMsgMetaErrorOutputBufferTooSmall"
	case C.SerializeRecvMsgMetaErrorFromAddrBufferNull:
		return "SerializeRecvMsgMetaErrorFromAddrBufferNull"
	case C.SerializeRecvMsgMetaErrorFromAddrBufferTooSmall:
		return "SerializeRecvMsgMetaErrorFromAddrBufferTooSmall"
	case C.SerializeRecvMsgMetaErrorFailedToEncode:
		return "SerializeRecvMsgMetaErrorFailedToEncode"
	default:
		panic(fmt.Sprintf("unknown serialization result %#v", err))
	}
}

// serializeRecvMsgMeta serializes metadata contained within `res` into `buf`
// as a RecvMsgMeta FIDL message using the LLCPP bindings.
func serializeRecvMsgMeta(protocol tcpip.NetworkProtocolNumber, res tcpip.ReadResult, buf []byte) {
	fidlAddr := toNetSocketAddress(protocol, res.RemoteAddr)
	var fromAddrType C.IpAddrType
	var addrBuf C.ConstBuffer
	var addrSlice []byte
	switch w := fidlAddr.Which(); w {
	case fidlnet.SocketAddressIpv4:
		fromAddrType = C.Ipv4
		addrSlice = fidlAddr.Ipv4.Address.Addr[:]
	case fidlnet.SocketAddressIpv6:
		fromAddrType = C.Ipv6
		addrSlice = fidlAddr.Ipv6.Address.Addr[:]
	default:
		panic(fmt.Sprintf("unrecognized socket address %d", w))
	}
	addrBuf = C.ConstBuffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&addrSlice))).Data)),
		buf_size: C.ulong(len(addrSlice)),
	}

	bufOut := C.Buffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&buf))).Data)),
		buf_size: C.ulong(len(buf)),
	}

	if res.Count > int(math.MaxUint16) {
		panic(fmt.Sprintf("payload size (%d) exceeds max allowed (%d)", res.Count, math.MaxUint16))
	}

	recv_meta := C.RecvMsgMeta{
		cmsg_set: C.CmsgSet{
			has_ip_tos:          C.bool(res.ControlMessages.HasTOS),
			ip_tos:              C.uchar(res.ControlMessages.TOS),
			has_ip_ttl:          C.bool(res.ControlMessages.HasTTL),
			ip_ttl:              C.uchar(res.ControlMessages.TTL),
			has_ipv6_tclass:     C.bool(res.ControlMessages.HasTClass),
			ipv6_tclass:         C.uchar(res.ControlMessages.TClass),
			has_ipv6_hoplimit:   C.bool(res.ControlMessages.HasHopLimit),
			ipv6_hoplimit:       C.uchar(res.ControlMessages.HopLimit),
			has_timestamp_nanos: C.bool(res.ControlMessages.HasTimestamp),
			timestamp_nanos:     C.long(res.ControlMessages.Timestamp.UnixNano()),
			has_ipv6_pktinfo:    C.bool(res.ControlMessages.HasIPv6PacketInfo),
			ipv6_pktinfo: C.Ipv6PktInfo{
				if_index: C.ulong(res.ControlMessages.IPv6PacketInfo.NIC),
			},
		},
		from_addr_type: fromAddrType,
		payload_size:   C.ushort(res.Count),
		port:           C.ushort(res.RemoteAddr.Port),
	}

	if res.ControlMessages.HasIPv6PacketInfo {
		header := (*reflect.SliceHeader)(unsafe.Pointer(&recv_meta.cmsg_set.ipv6_pktinfo.addr))
		copy(*(*[]byte)(unsafe.Pointer(&header)), res.ControlMessages.IPv6PacketInfo.Addr)
	}

	if err := C.serialize_recv_msg_meta(&recv_meta, addrBuf, bufOut); err != C.SerializeRecvMsgMetaErrorNone {
		panic(fmt.Sprintf("serialization error: %s", serializeRecvMsgMetaErrorToString(err)))
	}
}
