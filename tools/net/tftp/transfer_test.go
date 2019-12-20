// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tftp

import (
	"bytes"
	"encoding/binary"
	"io"
	"math"
	"net"
	"testing"
)

var handleAckTests = []struct {
	name        string
	currentSeq  uint32
	expectedSeq uint32
	nextSeq     uint16
	windowSize  uint16
}{
	{"Default", 0, 1, 1, 2},
	{"Rollover", math.MaxUint16, uint32(math.MaxUint16) + 1, 0, 2},
	{"Out of Order (Leading)", 0, 0, 10, 2},
	{"Out of Order (Trailing)", 20, 20, 10, 2},
}

func TestHandleAck(t *testing.T) {
	for _, test := range handleAckTests {
		t.Run(test.name, func(t *testing.T) {
			ackPacket := make([]byte, 4, 4)
			ackPacket[opCodeOffset] = opAck

			xfer := &transfer{opts: &options{
				windowSize: test.windowSize,
			}}

			xfer.seq = test.currentSeq
			binary.BigEndian.PutUint16(ackPacket[blockNumberOffset:], test.nextSeq)
			if err := expectAck(xfer, ackPacket, nil); err != nil {
				t.Errorf("Unexpected error returned: %v", err)
			}
			if xfer.seq != uint32(test.expectedSeq) {
				t.Errorf("Expected transfer sequence to be %d, is %d", test.expectedSeq, xfer.seq)
			}
		})
	}
}

var handleDataTests = []struct {
	name            string
	currentLastAck  uint32
	currentSeq      uint32
	expectedLastAck uint32
	expectedSeq     uint32
	nextSeq         uint16
	blockSize       uint16
	windowSize      uint16
	transferSize    uint64
	expectedErr     error
}{
	{"Default", 0, 1, 0, 2, 2, 1, 5, 10, nil},
	{"Out of Order (Leading)", 0, 1, 1, 1, 3, 1, 5, 10, nil},
	{"Out of Order (Trailing)", 0, 1, 0, 1, 0, 1, 5, 10, nil},
	{"End of Window", 0, 4, 5, 5, 5, 1, 5, 10, nil},
	{"Last Packet", 0, 0, 1, 1, 1, 1, 2, 1, io.EOF},
}

func TestHandleData(t *testing.T) {
	for _, test := range handleDataTests {
		t.Run(test.name, func(t *testing.T) {
			dataPacket := make([]byte, 4, 4+test.blockSize)
			dataPacket[opCodeOffset] = opData

			conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv6loopback})
			if err != nil {
				t.Errorf("Dummy conn failed to create %v", err)
			}

			defer conn.Close()

			xfer := &transfer{
				addr:   conn.LocalAddr().(*net.UDPAddr),
				buffer: bytes.NewBuffer([]byte{}),
				opts: &options{
					blockSize:    test.blockSize,
					windowSize:   test.windowSize,
					timeout:      timeout,
					transferSize: test.transferSize,
				},
				client: &ClientImpl{
					conn: conn,
				},
			}

			l := int(test.blockSize)
			// Create a partial packet if EOF
			if test.expectedErr == io.EOF {
				l--
			}

			for i := 0; i < l; i++ {
				dataPacket = append(dataPacket, 0xFF)
			}

			xfer.seq = test.currentSeq
			xfer.lastAck = test.currentLastAck
			binary.BigEndian.PutUint16(dataPacket[blockNumberOffset:], test.nextSeq)
			if err := expectData(xfer, dataPacket, nil); err != test.expectedErr {
				t.Errorf("Unexpected error returned: %v", err)
			}
			if xfer.lastAck != uint32(test.expectedLastAck) {
				t.Errorf("Expected lastAck to be %d, is %d", test.expectedLastAck, xfer.lastAck)
			}
			if xfer.seq != uint32(test.expectedSeq) {
				t.Errorf("Expected transfer sequence to be %d, is %d", test.expectedSeq, xfer.seq)
			}
		})
	}
}

func testHandleUnexpectedHelper(t *testing.T, err error) {
	if err == nil {
		t.Errorf("Expected error, but received nil")
	}
}

func TestHandleUnexpected(t *testing.T) {
	dataPacket := []byte{0, opData}
	ackPacket := []byte{0, opAck}
	oackPacket := []byte{0, opOack}

	xfer := &transfer{}

	t.Run("expectAck(); got data", func(t *testing.T) {
		testHandleUnexpectedHelper(t, expectAck(xfer, dataPacket, nil))
	})

	t.Run("expectAck(); got oack", func(t *testing.T) {
		testHandleUnexpectedHelper(t, expectAck(xfer, oackPacket, nil))
	})

	t.Run("expectData(); got ack", func(t *testing.T) {
		testHandleUnexpectedHelper(t, expectData(xfer, ackPacket, nil))
	})

	t.Run("expectData(); got oack", func(t *testing.T) {
		testHandleUnexpectedHelper(t, expectData(xfer, oackPacket, nil))
	})

	t.Run("expectOack(); got ack", func(t *testing.T) {
		testHandleUnexpectedHelper(t, expectOack(xfer, ackPacket, nil))
	})

	t.Run("expectOack(); got data", func(t *testing.T) {
		testHandleUnexpectedHelper(t, expectOack(xfer, dataPacket, nil))
	})
}
