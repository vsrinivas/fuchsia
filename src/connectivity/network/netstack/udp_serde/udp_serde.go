// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package udp_serde

import (
	fidlnet "fidl/fuchsia/net"
	"fmt"
	"math"
	"reflect"
	"time"
	"unsafe"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

// #cgo LDFLAGS: -ludp_serde_for_cgo -lc++ -lzircon
// #include <ifaddrs.h>
// #include <string.h>
// #include "udp_serde.h"
import "C"

func TxUdpPreludeSize() uint32 {
	return uint32(C.kTxUdpPreludeSize)
}

func RxUdpPreludeSize() uint32 {
	return uint32(C.kRxUdpPreludeSize)
}

func convertDeserializeSendMsgMetaErr(err C.DeserializeSendMsgMetaError) error {
	switch err {
	case C.DeserializeSendMsgMetaErrorInputBufferNull:
		return &InputBufferNullErr{}
	case C.DeserializeSendMsgMetaErrorInputBufferTooSmall:
		return &InputBufferTooSmallErr{}
	case C.DeserializeSendMsgMetaErrorNonZeroPrelude:
		return &NonZeroPreludeErr{}
	case C.DeserializeSendMsgMetaErrorFailedToDecode:
		return &FailedToDecodeErr{}
	default:
		panic(fmt.Sprintf("unknown deserialization result %#v", err))
	}
}

func getFidlAddrTypeAndSlice(fidlAddr fidlnet.SocketAddress) (C.IpAddrType, []byte) {
	switch w := fidlAddr.Which(); w {
	case fidlnet.SocketAddressIpv4:
		return C.Ipv4, fidlAddr.Ipv4.Address.Addr[:]
	case fidlnet.SocketAddressIpv6:
		return C.Ipv6, fidlAddr.Ipv6.Address.Addr[:]
	default:
		panic(fmt.Sprintf("unrecognized socket address %d", w))
	}
}

// DeserializeSendMsgMeta deserializes metadata contained within `buf`
// as a SendMsgMeta FIDL message using the LLCPP bindings.
//
// If the deserialized metadata contains an address, returns that address (else returns nil).
// Returns any found control messages present within a `tcpip.SendableControlMessages` struct.
func DeserializeSendMsgMeta(buf []byte) (*tcpip.FullAddress, tcpip.SendableControlMessages, error) {
	bufIn := C.Buffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&buf))).Data)),
		buf_size: C.ulong(len(buf)),
	}

	res := C.deserialize_send_msg_meta(bufIn)
	if res.err != C.DeserializeSendMsgMetaErrorNone {
		return nil, tcpip.SendableControlMessages{}, convertDeserializeSendMsgMetaErr(res.err)
	}

	addr := func() *tcpip.FullAddress {
		if res.has_addr {
			var addr tcpip.Address
			switch res.addr.addr_type {
			case C.Ipv4:
				src := res.addr.addr[:header.IPv4AddressSize]
				addr = tcpip.Address(*(*[]byte)(unsafe.Pointer(&src)))
			case C.Ipv6:
				src := res.addr.addr[:]
				addr = tcpip.Address(*(*[]byte)(unsafe.Pointer(&src)))
			}
			return &tcpip.FullAddress{
				Addr: addr,
				Port: uint16(res.port),
				NIC:  tcpip.NICID(res.zone_index),
			}
		}
		return nil
	}()

	var cmsgSet tcpip.SendableControlMessages
	if res.cmsg_set.has_ip_ttl {
		cmsgSet.HasTTL = true
		cmsgSet.TTL = uint8(res.cmsg_set.ip_ttl)
	}
	if res.cmsg_set.has_ipv6_hoplimit {
		cmsgSet.HasHopLimit = true
		cmsgSet.HopLimit = uint8(res.cmsg_set.ipv6_hoplimit)
	}
	if res.cmsg_set.has_ipv6_pktinfo {
		cmsgSet.HasIPv6PacketInfo = true
		src := res.cmsg_set.ipv6_pktinfo.addr[:]
		cmsgSet.IPv6PacketInfo = tcpip.IPv6PacketInfo{
			NIC:  tcpip.NICID(res.cmsg_set.ipv6_pktinfo.if_index),
			Addr: fidlconv.BytesToAddressDroppingUnspecified(*(*[]byte)(unsafe.Pointer(&src))),
		}
	}
	return addr, cmsgSet, nil
}

func convertSerializeRecvMsgMetaErr(err C.SerializeRecvMsgMetaError) error {
	switch err {
	case C.SerializeRecvMsgMetaErrorNone:
		return nil
	case C.SerializeRecvMsgMetaErrorOutputBufferNull:
		return &InputBufferNullErr{}
	case C.SerializeRecvMsgMetaErrorOutputBufferTooSmall:
		return &InputBufferTooSmallErr{}
	case C.SerializeRecvMsgMetaErrorFromAddrBufferNull:
		panic(fmt.Sprintf("got unexpected C.SerializeRecvMsgMetaErrorFromAddrBufferNull error"))
	case C.SerializeRecvMsgMetaErrorFromAddrBufferTooSmall:
		panic(fmt.Sprintf("got unexpected C.SerializeRecvMsgMetaErrorFromAddrBufferTooSmall error"))
	case C.SerializeRecvMsgMetaErrorFailedToEncode:
		return &FailedToEncodeErr{}
	default:
		panic(fmt.Sprintf("unknown deserialization result %#v", err))
	}
}

// SerializeRecvMsgMeta serializes metadata contained within `res` into `buf`
// as a RecvMsgMeta FIDL message using the LLCPP bindings.
func SerializeRecvMsgMeta(protocol tcpip.NetworkProtocolNumber, res tcpip.ReadResult, buf []byte) error {
	fidlAddr := fidlconv.ToNetSocketAddressWithProto(protocol, res.RemoteAddr)
	fromAddrType, addrSlice := getFidlAddrTypeAndSlice(fidlAddr)
	addrBuf := C.ConstBuffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&addrSlice))).Data)),
		buf_size: C.ulong(len(addrSlice)),
	}

	bufOut := C.Buffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&buf))).Data)),
		buf_size: C.ulong(len(buf)),
	}

	if res.Count > int(math.MaxUint16) {
		return &PayloadSizeExceedsMaxAllowedErr{payloadSize: res.Count, maxAllowed: math.MaxUint16}
	}

	recv_meta := C.RecvMsgMeta{
		cmsg_set: C.RecvCmsgSet{
			send_and_recv: C.SendAndRecvCmsgSet{
				has_ip_ttl:        C.bool(res.ControlMessages.HasTTL),
				ip_ttl:            C.uchar(res.ControlMessages.TTL),
				has_ipv6_hoplimit: C.bool(res.ControlMessages.HasHopLimit),
				ipv6_hoplimit:     C.uchar(res.ControlMessages.HopLimit),
				has_ipv6_pktinfo:  C.bool(res.ControlMessages.HasIPv6PacketInfo),
				ipv6_pktinfo: C.Ipv6PktInfo{
					if_index: C.ulong(res.ControlMessages.IPv6PacketInfo.NIC),
				},
			},
			has_timestamp_nanos: C.bool(res.ControlMessages.HasTimestamp),
			timestamp_nanos:     C.long(res.ControlMessages.Timestamp.UnixNano()),
			has_ip_tos:          C.bool(res.ControlMessages.HasTOS),
			ip_tos:              C.uchar(res.ControlMessages.TOS),
			has_ipv6_tclass:     C.bool(res.ControlMessages.HasTClass),
			ipv6_tclass:         C.uchar(res.ControlMessages.TClass),
		},
		addr_type:    fromAddrType,
		payload_size: C.ushort(res.Count),
		port:         C.ushort(res.RemoteAddr.Port),
	}

	if fidlAddr.Which() == fidlnet.SocketAddressIpv6 {
		recv_meta.zone_index = C.ulong(fidlAddr.Ipv6.ZoneIndex)
	}

	if res.ControlMessages.HasIPv6PacketInfo {
		dst := recv_meta.cmsg_set.send_and_recv.ipv6_pktinfo.addr[:]
		copy(*(*[]byte)(unsafe.Pointer(&dst)), res.ControlMessages.IPv6PacketInfo.Addr)
	}

	return convertSerializeRecvMsgMetaErr(C.serialize_recv_msg_meta(&recv_meta, addrBuf, bufOut))
}

func convertDeserializeRecvMsgMetaErr(err C.DeserializeRecvMsgMetaError) error {
	switch err {
	case C.DeserializeRecvMsgMetaErrorNone:
		return nil
	case C.DeserializeRecvMsgMetaErrorInputBufferNull:
		return &InputBufferNullErr{}
	case C.DeserializeRecvMsgMetaErrorUnspecifiedDecodingFailure:
		return &UnspecifiedDecodingFailure{}
	default:
		panic(fmt.Sprintf("unknown deserialization error: %#v", err))
	}
}

type RecvMsgMeta struct {
	addr        *tcpip.FullAddress
	control     tcpip.ReceivableControlMessages
	payloadSize uint16
}

// DeserializeRecvMsgMeta deserializes metadata contained within `buf`
// as a RecvMsgMeta FIDL message using the LLCPP bindings.
//
// If the deserialized metadata contains an address, returns that address (else returns nil).
// Returns any found control messages present within a `tcpip.ReceiveableControlMessages` struct.
//
// This method is only intended to be used in tests.
// TODO(https://fxbug.dev/107864): Isolate testonly methods.
func DeserializeRecvMsgMeta(buf []byte) (RecvMsgMeta, error) {
	bufIn := C.Buffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&buf))).Data)),
		buf_size: C.ulong(len(buf)),
	}

	res := C.deserialize_recv_msg_meta(bufIn)
	if res.err != C.DeserializeRecvMsgMetaErrorNone {
		return RecvMsgMeta{}, convertDeserializeRecvMsgMetaErr(res.err)
	}

	addr := func() *tcpip.FullAddress {
		if res.has_addr {
			var addr tcpip.Address
			switch res.addr.addr_type {
			case C.Ipv4:
				src := res.addr.addr[:header.IPv4AddressSize]
				addr = tcpip.Address(*(*[]byte)(unsafe.Pointer(&src)))
			case C.Ipv6:
				src := res.addr.addr[:]
				addr = tcpip.Address(*(*[]byte)(unsafe.Pointer(&src)))
			}
			return &tcpip.FullAddress{
				Addr: addr,
				Port: uint16(res.port),
				NIC:  tcpip.NICID(res.zone_index),
			}
		}
		return nil
	}

	var cmsgSet tcpip.ReceivableControlMessages
	if res.cmsg_set.has_timestamp_nanos {
		cmsgSet.HasTimestamp = true
		cmsgSet.Timestamp = time.Unix(0, int64(res.cmsg_set.timestamp_nanos))
	}
	if res.cmsg_set.has_ip_tos {
		cmsgSet.HasTOS = true
		cmsgSet.TOS = uint8(res.cmsg_set.ip_tos)
	}
	if res.cmsg_set.has_ipv6_tclass {
		cmsgSet.HasTClass = true
		cmsgSet.TClass = uint32(res.cmsg_set.ipv6_tclass)
	}
	if res.cmsg_set.send_and_recv.has_ip_ttl {
		cmsgSet.HasTTL = true
		cmsgSet.TTL = uint8(res.cmsg_set.send_and_recv.ip_ttl)
	}
	if res.cmsg_set.send_and_recv.has_ipv6_hoplimit {
		cmsgSet.HasHopLimit = true
		cmsgSet.HopLimit = uint8(res.cmsg_set.send_and_recv.ipv6_hoplimit)
	}
	if res.cmsg_set.send_and_recv.has_ipv6_pktinfo {
		cmsgSet.HasIPv6PacketInfo = true
		src := res.cmsg_set.send_and_recv.ipv6_pktinfo.addr[:]
		cmsgSet.IPv6PacketInfo = tcpip.IPv6PacketInfo{
			NIC:  tcpip.NICID(res.cmsg_set.send_and_recv.ipv6_pktinfo.if_index),
			Addr: tcpip.Address(*(*[]byte)(unsafe.Pointer(&src))),
		}
	}
	return RecvMsgMeta{
		addr:        addr(),
		control:     cmsgSet,
		payloadSize: uint16(res.payload_size),
	}, nil
}

func convertSerializeSendMsgMetaErr(err C.SerializeSendMsgMetaError) error {
	switch err {
	case C.SerializeSendMsgMetaErrorNone:
		return nil
	case C.SerializeSendMsgMetaErrorOutputBufferNull:
		return &InputBufferNullErr{}
	case C.SerializeSendMsgMetaErrorOutputBufferTooSmall:
		return &InputBufferTooSmallErr{}
	case C.SerializeSendMsgMetaErrorFailedToEncode:
		return &FailedToEncodeErr{}
	case C.SerializeSendMsgMetaErrorAddrBufferNull:
		panic(fmt.Sprintf("got unexpected C.SerializeSendMsgMetaErrorAddrBufferNull error"))
	case C.SerializeSendMsgMetaErrorAddrBufferSizeMismatch:
		panic(fmt.Sprintf("got unexpected C.SerializeSendMsgMetaErrorAddrBufferSizeMismatch error"))
	default:
		panic(fmt.Sprintf("unknown serialization result %#v", err))
	}
}

// SerializeSendMsgMeta serializes `addr` and `cmsg_set` into `buf` as a SendMsgMeta FIDL message
// using the LLCPP bindings.
//
// This method is only intended to be used in tests.
// TODO(https://fxbug.dev/107864): Isolate testonly methods.
func SerializeSendMsgMeta(protocol tcpip.NetworkProtocolNumber, addr tcpip.FullAddress, cmsgSet tcpip.SendableControlMessages, buf []byte) error {
	fidlAddr := fidlconv.ToNetSocketAddressWithProto(protocol, addr)
	fromAddrType, addrSlice := getFidlAddrTypeAndSlice(fidlAddr)
	meta := C.SendMsgMeta{
		addr_type: fromAddrType,
		port:      C.ushort(addr.Port),
		cmsg_set: C.SendAndRecvCmsgSet{
			has_ip_ttl:        C.bool(cmsgSet.HasTTL),
			ip_ttl:            C.uchar(cmsgSet.TTL),
			has_ipv6_hoplimit: C.bool(cmsgSet.HasHopLimit),
			ipv6_hoplimit:     C.uchar(cmsgSet.HopLimit),
			has_ipv6_pktinfo:  C.bool(cmsgSet.HasIPv6PacketInfo),
			ipv6_pktinfo: C.Ipv6PktInfo{
				if_index: C.ulong(cmsgSet.IPv6PacketInfo.NIC),
			},
		},
	}

	if fidlAddr.Which() == fidlnet.SocketAddressIpv6 {
		meta.zone_index = C.ulong(fidlAddr.Ipv6.ZoneIndex)
	}

	addrBuf := C.ConstBuffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&addrSlice))).Data)),
		buf_size: C.ulong(len(addrSlice)),
	}

	if cmsgSet.HasIPv6PacketInfo {
		dst := meta.cmsg_set.ipv6_pktinfo.addr[:]
		copy(*(*[]byte)(unsafe.Pointer(&dst)), cmsgSet.IPv6PacketInfo.Addr[:])
	}

	bufOut := C.Buffer{
		buf:      (*C.uchar)(unsafe.Pointer(((*reflect.SliceHeader)(unsafe.Pointer(&buf))).Data)),
		buf_size: C.ulong(len(buf)),
	}

	return convertSerializeSendMsgMetaErr(C.serialize_send_msg_meta(&meta, addrBuf, bufOut))
}
