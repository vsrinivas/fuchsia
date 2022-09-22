// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package udp_serde

import (
	"errors"
	"fmt"
	"math"
	"testing"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

const preludeOffset = 8

const (
	ipv4Loopback       tcpip.Address = "\x7f\x00\x00\x01"
	ipv6Loopback       tcpip.Address = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
	ipv6LinkLocal      tcpip.Address = "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	testPort           uint16        = 42
	testIpTtl          uint8         = 43
	testIpv6Hoplimit   uint8         = 44
	testNICID          tcpip.NICID   = 45
	testIpTos          uint8         = 46
	testIpv6Tclass     uint32        = 47
	testTimestampNanos int64         = 48
	testPayloadSize    int           = 49
	invalidIpTtl       uint8         = 0
)

func TestSerializeThenDeserializeSendMsgMeta(t *testing.T) {
	for _, netProto := range []tcpip.NetworkProtocolNumber{
		header.IPv4ProtocolNumber,
		header.IPv6ProtocolNumber,
	} {
		t.Run(fmt.Sprintf("%d", netProto), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())
			addr := tcpip.FullAddress{
				Port: testPort,
			}
			var cmsgSet tcpip.SendableControlMessages

			switch netProto {
			case header.IPv4ProtocolNumber:
				addr.Addr = ipv4Loopback
				cmsgSet.HasTTL = true
				cmsgSet.TTL = testIpTtl
			case header.IPv6ProtocolNumber:
				addr.Addr = ipv6Loopback
				cmsgSet.HasHopLimit = true
				cmsgSet.HopLimit = testIpv6Hoplimit
				cmsgSet.HasIPv6PacketInfo = true
				cmsgSet.IPv6PacketInfo = tcpip.IPv6PacketInfo{
					NIC:  testNICID,
					Addr: ipv6Loopback,
				}
				addr.NIC = testNICID
			}

			if err := SerializeSendMsgMeta(tcpip.NetworkProtocolNumber(netProto), addr, cmsgSet, buf); err != nil {
				t.Fatalf("got SerializeSendMsgMeta(%d, %#v, %#v, _) = (%#v), want (%#v)", netProto, addr, cmsgSet, err, nil)
			}

			deserializedAddr, deserializedCmsgSet, err := DeserializeSendMsgMeta(buf)

			if err != nil {
				t.Fatalf("expect DeserializeSendMsgMeta(_) succeeds, got: %s", err)
			}

			wantAddr := tcpip.FullAddress{
				Port: addr.Port,
				Addr: addr.Addr,
				// Expect the NICID set in the IPv6 case above is not serialized because the address is non-link local.
				NIC: 0,
			}

			if got, want := *deserializedAddr, wantAddr; got != want {
				t.Errorf("got address after serde = (%#v), want (%#v)", got, want)
			}

			if got, want := deserializedCmsgSet, cmsgSet; got != want {
				t.Errorf("got cmsg set after serde = (%#v), want (%#v)", got, want)
			}
		})
	}
}

func TestSerializeThenDeserializeSendMsgMetaWithLinkLocalIPv6Addr(t *testing.T) {
	buf := make([]byte, TxUdpPreludeSize())
	addr := tcpip.FullAddress{
		Port: testPort,
		Addr: ipv6LinkLocal,
		NIC:  testNICID,
	}
	var cmsgSet tcpip.SendableControlMessages

	if err := SerializeSendMsgMeta(ipv6.ProtocolNumber, addr, cmsgSet, buf); err != nil {
		t.Fatalf("got SerializeSendMsgMeta(%d, %#v, %#v, _) = (%#v), want (%#v)", ipv6.ProtocolNumber, addr, cmsgSet, err, nil)
	}

	deserializedAddr, _, err := DeserializeSendMsgMeta(buf)

	if err != nil {
		t.Fatalf("expect DeserializeSendMsgMeta(_) succeeds, got: %s", err)
	}

	if got, want := *deserializedAddr, addr; got != want {
		t.Errorf("got address after serde = (%#v), want (%#v)", got, want)
	}
}

func TestSerializeThenDeserializeSendMsgMetaWithUnspecifiedIPv6PacketInfoAddr(t *testing.T) {
	buf := make([]byte, TxUdpPreludeSize())
	addr := tcpip.FullAddress{
		Port: testPort,
		Addr: ipv6LinkLocal,
		NIC:  testNICID,
	}
	cmsgSet := tcpip.SendableControlMessages{
		HasIPv6PacketInfo: true,
		IPv6PacketInfo: tcpip.IPv6PacketInfo{
			Addr: tcpip.Address(""),
			NIC:  testNICID,
		},
	}

	if err := SerializeSendMsgMeta(ipv6.ProtocolNumber, addr, cmsgSet, buf); err != nil {
		t.Fatalf("got SerializeSendMsgMeta(%d, %#v, %#v, _) = (%#v), want (%#v)", ipv6.ProtocolNumber, addr, cmsgSet, err, nil)
	}

	deserializedAddr, deserializedCmsg, err := DeserializeSendMsgMeta(buf)

	if err != nil {
		t.Fatalf("expect DeserializeSendMsgMeta(_) succeeds, got: %s", err)
	}
	if got, want := *deserializedAddr, addr; got != want {
		t.Errorf("got address after serde = (%#v), want (%#v)", got, want)
	}
	if got, want := deserializedCmsg, cmsgSet; got != want {
		t.Errorf("got cmsg after serde = (%#v), want (%#v)", got, want)
	}
}

func TestSerializeSendMsgMetaFailures(t *testing.T) {
	for _, testCase := range []struct {
		name        string
		getBuffer   func([]byte) []byte
		expectedErr error
	}{
		{"nil buffer", func(buf []byte) []byte { return nil }, &InputBufferNullErr{}},
		{"buffer too small", func(buf []byte) []byte { return buf[:preludeOffset-1] }, &InputBufferTooSmallErr{}},
	} {

		t.Run(fmt.Sprintf("%s", testCase.name), func(t *testing.T) {
			storage := make([]byte, TxUdpPreludeSize())
			buf := testCase.getBuffer(storage)

			addr := tcpip.FullAddress{
				Port: testPort,
				Addr: ipv4Loopback,
			}
			cmsgSet := tcpip.SendableControlMessages{}

			err := SerializeSendMsgMeta(header.IPv4ProtocolNumber, addr, cmsgSet, buf)

			if got, want := err, testCase.expectedErr; !errors.Is(err, testCase.expectedErr) {
				t.Errorf("got SerializeSendMsgMeta(%d, %#v, %#v, _) = (%#v), want (%#v)", header.IPv4ProtocolNumber, addr, cmsgSet, got, want)
			}
		})
	}
}

func TestDeserializeSendMsgMetaFailures(t *testing.T) {
	type DeserializeSendMsgMetaErrorCondition int

	const (
		DeserializeSendMsgMetaErrInputBufferNil DeserializeSendMsgMetaErrorCondition = iota
		DeserializeSendMsgMetaErrInputBufferTooSmall
		DeserializeSendMsgMetaErrNonZeroPrelude
		DeserializeSendMsgMetaErrFailedToDecode
	)
	for _, testCase := range []struct {
		name         string
		errCondition DeserializeSendMsgMetaErrorCondition
		expectedErr  error
	}{
		{"nil buffer", DeserializeSendMsgMetaErrInputBufferNil, &InputBufferNullErr{}},
		{"buffer too small", DeserializeSendMsgMetaErrInputBufferTooSmall, &InputBufferTooSmallErr{}},
		{"nonzero prelude", DeserializeSendMsgMetaErrNonZeroPrelude, &NonZeroPreludeErr{}},
		{"failed to decode", DeserializeSendMsgMetaErrFailedToDecode, &FailedToDecodeErr{}},
	} {

		t.Run(fmt.Sprintf("%s", testCase.name), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())

			switch DeserializeSendMsgMetaErrorCondition(testCase.errCondition) {
			case DeserializeSendMsgMetaErrInputBufferNil:
				buf = nil
			case DeserializeSendMsgMetaErrInputBufferTooSmall:
				buf = buf[:preludeOffset-1]
			case DeserializeSendMsgMetaErrNonZeroPrelude:
				buf[preludeOffset] = 1
			case DeserializeSendMsgMetaErrFailedToDecode:
			}

			_, _, err := DeserializeSendMsgMeta(buf)

			if got, want := err, testCase.expectedErr; !errors.Is(err, testCase.expectedErr) {
				t.Errorf("got DeserializeSendMsgMeta(_) = (_, _, %#v), want (_, _, %#v)", got, want)
			}
		})
	}
}

func TestSerializeRecvMsgMetaFailures(t *testing.T) {
	type SerializeRecvMsgMetaErrorCondition int

	const (
		SerializeRecvMsgMetaErrOutputBufferNil SerializeRecvMsgMetaErrorCondition = iota
		SerializeRecvMsgMetaErrOutputBufferTooSmall
		SerializeRecvMsgMetaErrPayloadTooLarge
	)

	const maxPayloadSize = int(math.MaxUint16)
	const tooBigPayloadSize = maxPayloadSize + 1

	for _, testCase := range []struct {
		name         string
		errCondition SerializeRecvMsgMetaErrorCondition
		expectedErr  error
	}{
		{"nil buffer", SerializeRecvMsgMetaErrOutputBufferNil, &InputBufferNullErr{}},
		{"buffer too small", SerializeRecvMsgMetaErrOutputBufferTooSmall, &InputBufferTooSmallErr{}},
		{"payload too large", SerializeRecvMsgMetaErrPayloadTooLarge, &PayloadSizeExceedsMaxAllowedErr{payloadSize: tooBigPayloadSize, maxAllowed: maxPayloadSize}},
	} {
		for _, netProto := range []tcpip.NetworkProtocolNumber{
			header.IPv4ProtocolNumber,
			header.IPv6ProtocolNumber,
		} {
			t.Run(fmt.Sprintf("%s %d", testCase.name, netProto), func(t *testing.T) {
				res := tcpip.ReadResult{}
				buf := make([]byte, TxUdpPreludeSize())

				switch SerializeRecvMsgMetaErrorCondition(testCase.errCondition) {
				case SerializeRecvMsgMetaErrOutputBufferNil:
					buf = nil
				case SerializeRecvMsgMetaErrOutputBufferTooSmall:
					buf = buf[:preludeOffset-1]
				case SerializeRecvMsgMetaErrPayloadTooLarge:
					res.Count = tooBigPayloadSize
				}

				err := SerializeRecvMsgMeta(tcpip.NetworkProtocolNumber(netProto), res, buf)

				if got, want := err, testCase.expectedErr; !errors.Is(err, testCase.expectedErr) {
					t.Errorf("got SerializeRecvMsgMeta(%d, %#v, _) = (%#v), want (%#v)", netProto, res, got, want)
				}
			})
		}
	}
}

func TestSerializeThenDeserializeRecvMsgMeta(t *testing.T) {
	for _, netProto := range []tcpip.NetworkProtocolNumber{
		header.IPv4ProtocolNumber,
		header.IPv6ProtocolNumber,
	} {
		t.Run(fmt.Sprintf("%d", netProto), func(t *testing.T) {
			addr := tcpip.FullAddress{
				Port: testPort,
			}
			cmsgSet := tcpip.ReceivableControlMessages{
				HasTimestamp: true,
				Timestamp:    time.Unix(0, testTimestampNanos),
			}

			switch netProto {
			case header.IPv4ProtocolNumber:
				addr.Addr = ipv4Loopback
				cmsgSet.HasTTL = true
				cmsgSet.TTL = testIpTtl
				cmsgSet.HasTOS = true
				cmsgSet.TOS = testIpTos
			case header.IPv6ProtocolNumber:
				addr.Addr = ipv6Loopback
				cmsgSet.HasHopLimit = true
				cmsgSet.HopLimit = testIpv6Hoplimit
				cmsgSet.HasIPv6PacketInfo = true
				cmsgSet.IPv6PacketInfo = tcpip.IPv6PacketInfo{
					NIC:  testNICID,
					Addr: ipv6Loopback,
				}
				addr.NIC = testNICID
				cmsgSet.HasTClass = true
				cmsgSet.TClass = testIpv6Tclass
			}
			res := tcpip.ReadResult{
				ControlMessages: cmsgSet,
				RemoteAddr:      addr,
				Count:           testPayloadSize,
			}
			buf := make([]byte, RxUdpPreludeSize())

			if err := SerializeRecvMsgMeta(tcpip.NetworkProtocolNumber(netProto), res, buf); err != nil {
				t.Errorf("got SerializeRecvMsgMeta(%d, %#v, _) = (%#v), want (%#v)", netProto, res, err, nil)
			}

			recvMeta, err := DeserializeRecvMsgMeta(buf)

			if err != nil {
				t.Fatalf("expect DeserializeRecvMsgMeta(_) succeeds, got: %s", err)
			}

			wantAddr := tcpip.FullAddress{
				Port: res.RemoteAddr.Port,
				Addr: res.RemoteAddr.Addr,
				// Expect the NICID set in the IPv6 case above is not serialized because the address is non-link local.
				NIC: 0,
			}

			if got, want := *recvMeta.addr, wantAddr; got != want {
				t.Errorf("got address after serde = (%#v), want (%#v)", got, want)
			}

			if got, want := recvMeta.control, res.ControlMessages; got != want {
				t.Errorf("got cmsg set after serde = (%#v), want (%#v)", got, want)
			}

			if got, want := recvMeta.payloadSize, uint16(res.Count); got != want {
				t.Errorf("got payload size after serde = (%d), want (%d)", got, want)
			}
		})
	}
}

func TestSerializeThenDeserializeRecvMsgMetaWithLinkLocalIPv6Addr(t *testing.T) {
	buf := make([]byte, RxUdpPreludeSize())
	readResult := tcpip.ReadResult{
		RemoteAddr: tcpip.FullAddress{
			Port: testPort,
			Addr: ipv6LinkLocal,
			NIC:  testNICID,
		},
		ControlMessages: tcpip.ReceivableControlMessages{},
	}

	if err := SerializeRecvMsgMeta(ipv6.ProtocolNumber, readResult, buf); err != nil {
		t.Fatalf("got SerializeRecvMsgMeta(%d, %#v, _) = (%#v), want (%#v)", ipv6.ProtocolNumber, readResult, err, nil)
	}

	res, err := DeserializeRecvMsgMeta(buf)

	if err != nil {
		t.Fatalf("expect DeserializeRecvMsgMeta(_) succeeds, got: %s", err)
	}

	if got, want := *res.addr, readResult.RemoteAddr; got != want {
		t.Errorf("got address after serde = (%#v), want (%#v)", got, want)
	}
}

func TestDeserializeRecvMsgMetaFailures(t *testing.T) {
	type DeserializeRecvMsgMetaErrorCondition int

	const (
		DeserializeRecvMsgMetaErrInputBufferNil DeserializeRecvMsgMetaErrorCondition = iota
		DeserializeRecvMsgMetaErrInputBufferTooSmall
		DeserializeRecvMsgMetaErrNonZeroPrelude
		DeserializeRecvMsgMetaErrFailedToDecode
	)
	for _, testCase := range []struct {
		name         string
		errCondition DeserializeRecvMsgMetaErrorCondition
		expectedErr  error
	}{
		{"nil buffer", DeserializeRecvMsgMetaErrInputBufferNil, &InputBufferNullErr{}},
		{"buffer too small", DeserializeRecvMsgMetaErrInputBufferTooSmall, &UnspecifiedDecodingFailure{}},
		{"nonzero prelude", DeserializeRecvMsgMetaErrNonZeroPrelude, &UnspecifiedDecodingFailure{}},
		{"failed to decode", DeserializeRecvMsgMetaErrFailedToDecode, &UnspecifiedDecodingFailure{}},
	} {

		t.Run(fmt.Sprintf("%s", testCase.name), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())

			switch DeserializeRecvMsgMetaErrorCondition(testCase.errCondition) {
			case DeserializeRecvMsgMetaErrInputBufferNil:
				buf = nil
			case DeserializeRecvMsgMetaErrInputBufferTooSmall:
				buf = buf[:preludeOffset-1]
			case DeserializeRecvMsgMetaErrNonZeroPrelude:
				buf[preludeOffset] = 1
			case DeserializeRecvMsgMetaErrFailedToDecode:
			}

			_, err := DeserializeRecvMsgMeta(buf)

			if got, want := err, testCase.expectedErr; !errors.Is(err, testCase.expectedErr) {
				t.Errorf("got DeserializeRecvMsgMeta(_) = (_, _, %#v), want (_, _, %#v)", got, want)
			}
		})
	}
}
