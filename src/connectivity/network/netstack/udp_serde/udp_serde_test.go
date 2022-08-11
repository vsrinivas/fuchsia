// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package udp_serde

import (
	"errors"
	"fmt"
	"math"
	"testing"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

const preludeOffset = 8

type DeserializeSendMsgAddressErrorCondition int

const (
	DeserializeSendMsgAddressErrInputBufferNil DeserializeSendMsgAddressErrorCondition = iota
	DeserializeSendMsgAddressErrInputBufferTooSmall
	DeserializeSendMsgAddressErrNonZeroPrelude
	DeserializeSendMsgAddressErrFailedToDecode
)

func TestDeserializeSendMsgAddressFailures(t *testing.T) {
	for _, testCase := range []struct {
		name         string
		errCondition DeserializeSendMsgAddressErrorCondition
		expectedErr  error
	}{
		{"nil buffer", DeserializeSendMsgAddressErrInputBufferNil, &InputBufferNullErr{}},
		{"buffer too small", DeserializeSendMsgAddressErrInputBufferTooSmall, &InputBufferTooSmallErr{}},
		{"nonzero prelude", DeserializeSendMsgAddressErrNonZeroPrelude, &NonZeroPreludeErr{}},
		{"failed to decode", DeserializeSendMsgAddressErrFailedToDecode, &FailedToDecodeErr{}},
	} {

		t.Run(fmt.Sprintf("%s", testCase.name), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())

			switch DeserializeSendMsgAddressErrorCondition(testCase.errCondition) {
			case DeserializeSendMsgAddressErrInputBufferNil:
				buf = nil
			case DeserializeSendMsgAddressErrInputBufferTooSmall:
				buf = buf[:preludeOffset-1]
			case DeserializeSendMsgAddressErrNonZeroPrelude:
				buf[preludeOffset] = 1
			case DeserializeSendMsgAddressErrFailedToDecode:
			}

			_, err := DeserializeSendMsgAddress(buf)

			if err == nil {
				t.Errorf("expect DeserializeSendMsgAddress(_) fails")
			}

			if !errors.Is(err, testCase.expectedErr) {
				t.Errorf("DeserializeSendMsgAddress(_) failed with error: %#v, expected: %#v", err, testCase.expectedErr)
			}

		})
	}
}

type SerializeRecvMsgMetaErrorCondition int

const (
	SerializeRecvMsgMetaErrOutputBufferNil SerializeRecvMsgMetaErrorCondition = iota
	SerializeRecvMsgMetaErrOutputBufferTooSmall
	SerializeRecvMsgMetaErrPayloadTooLarge
)

func TestSerializeRecvMsgMetaFailures(t *testing.T) {
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

				if err == nil {
					t.Errorf("expect SerializeRecvMsgMeta(%d, %#v, _) fails", netProto, res)
				}

				if !errors.Is(err, testCase.expectedErr) {
					t.Errorf("SerializeRecvMsgMeta(%d, %#v, _) failed with error: %#v, expected: %#v", netProto, res, err, testCase.expectedErr)
				}
			})
		}
	}
}

func TestSerializeRecvMsgMetaSuccess(t *testing.T) {
	for _, netProto := range []tcpip.NetworkProtocolNumber{
		header.IPv4ProtocolNumber,
		header.IPv6ProtocolNumber,
	} {
		t.Run(fmt.Sprintf("%d", netProto), func(t *testing.T) {
			res := tcpip.ReadResult{}
			buf := make([]byte, RxUdpPreludeSize())

			if err := SerializeRecvMsgMeta(tcpip.NetworkProtocolNumber(netProto), res, buf); err != nil {
				t.Errorf("expect SerializeRecvMsgMeta(%d, %#v, _) succeeds, got: %s", netProto, res, err)
			}
		})
	}
}

const (
	ipv4Loopback tcpip.Address = "\x7f\x00\x00\x01"
	ipv6Loopback tcpip.Address = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
)

func TestSerializeSendFromAddrThenDeserialize(t *testing.T) {
	for _, netProto := range []tcpip.NetworkProtocolNumber{
		header.IPv4ProtocolNumber,
		header.IPv6ProtocolNumber,
	} {
		t.Run(fmt.Sprintf("%d", netProto), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())
			addr := tcpip.FullAddress{
				Port: 42,
			}
			switch netProto {
			case header.IPv4ProtocolNumber:
				addr.Addr = ipv4Loopback
			case header.IPv6ProtocolNumber:
				addr.Addr = ipv6Loopback
			}

			if err := SerializeSendMsgAddress(tcpip.NetworkProtocolNumber(netProto), addr, buf); err != nil {
				t.Fatalf("expect SerializeSendMsgAddress(%d, %#v, _) succeeds, got: %s", netProto, addr, err)
			}

			deserializedAddr, err := DeserializeSendMsgAddress(buf)

			if err != nil {
				t.Fatalf("expect DeserializeSendMsgAddress(_) succeeds, got: %s", err)
			}

			if addr != *deserializedAddr {
				t.Errorf("address after serde (%#v) != address before serde (%#v)", *deserializedAddr, addr)
			}
		})
	}
}

func TestSerializeSendFromAddrFailures(t *testing.T) {
	for _, testCase := range []struct {
		name        string
		setupBuffer func(*[]byte)
		expectedErr error
	}{
		{"nil buffer", func(buf *[]byte) { *buf = nil }, &InputBufferNullErr{}},
		{"buffer too small", func(buf *[]byte) { *buf = (*buf)[:preludeOffset-1] }, &InputBufferTooSmallErr{}},
	} {

		t.Run(fmt.Sprintf("%s", testCase.name), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())
			testCase.setupBuffer(&buf)

			addr := tcpip.FullAddress{
				Port: 42,
				Addr: ipv4Loopback,
			}

			err := SerializeSendMsgAddress(header.IPv4ProtocolNumber, addr, buf)

			if err == nil {
				t.Errorf("expect SerializeSendMsgAddress(%d, %#v, _) fails", header.IPv4ProtocolNumber, addr)
			}

			if !errors.Is(err, testCase.expectedErr) {
				t.Errorf("SerializeSendMsgAddress(%d, %#v, _) failed with error: %#v, expected: %#v", header.IPv4ProtocolNumber, addr, err, testCase.expectedErr)
			}
		})
	}
}
