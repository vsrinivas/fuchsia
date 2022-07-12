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
	InputBufferNil DeserializeSendMsgAddressErrorCondition = iota
	InputBufferTooSmall
	NonZeroPrelude
	FailedToDecode
)

func TestDeserializeSendMsgAddressFailures(t *testing.T) {
	for _, testCase := range []struct {
		name         string
		errCondition DeserializeSendMsgAddressErrorCondition
		expectedErr  error
	}{
		{"nil buffer", InputBufferNil, InputBufferNullErr{}},
		{"buffer too small", InputBufferTooSmall, InputBufferTooSmallErr{}},
		{"nonzero prelude", NonZeroPrelude, NonZeroPreludeErr{}},
		{"failed to decode", FailedToDecode, FailedToDecodeErr{}},
	} {

		t.Run(fmt.Sprintf("%s", testCase.name), func(t *testing.T) {
			buf := make([]byte, TxUdpPreludeSize())

			switch DeserializeSendMsgAddressErrorCondition(testCase.errCondition) {
			case InputBufferNil:
				buf = nil
			case InputBufferTooSmall:
				buf = buf[:preludeOffset-1]
			case NonZeroPrelude:
				buf[preludeOffset] = 1
			case FailedToDecode:
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
	OutputBufferNil SerializeRecvMsgMetaErrorCondition = iota
	OutputBufferTooSmall
	PayloadTooLarge
)

func TestSerializeRecvMsgMetaFailures(t *testing.T) {
	const maxPayloadSize = int(math.MaxUint16)
	const tooBigPayloadSize = maxPayloadSize + 1

	for _, testCase := range []struct {
		name         string
		errCondition SerializeRecvMsgMetaErrorCondition
		expectedErr  error
	}{
		{"nil buffer", OutputBufferNil, InputBufferNullErr{}},
		{"buffer too small", OutputBufferTooSmall, InputBufferTooSmallErr{}},
		{"payload too large", PayloadTooLarge, PayloadSizeExceedsMaxAllowedErr{payloadSize: tooBigPayloadSize, maxAllowed: maxPayloadSize}},
	} {
		for _, netProto := range []tcpip.NetworkProtocolNumber{
			header.IPv4ProtocolNumber,
			header.IPv6ProtocolNumber,
		} {
			t.Run(fmt.Sprintf("%s %d", testCase.name, netProto), func(t *testing.T) {
				res := tcpip.ReadResult{}
				buf := make([]byte, TxUdpPreludeSize())

				switch SerializeRecvMsgMetaErrorCondition(testCase.errCondition) {
				case OutputBufferNil:
					buf = nil
				case OutputBufferTooSmall:
					buf = buf[:preludeOffset-1]
				case PayloadTooLarge:
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
