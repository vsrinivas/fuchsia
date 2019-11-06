// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"bytes"
	"syscall/zx"
	"testing"
	"time"

	"netstack/link/eth"

	"fidl/fuchsia/hardware/ethernet"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/stack"
)

type deliverNetworkPacketArgs struct {
	srcLinkAddr, dstLinkAddr tcpip.LinkAddress
	protocol                 tcpip.NetworkProtocolNumber
	vv                       buffer.VectorisedView
	linkHeader               buffer.View
}

type dispatcherChan chan deliverNetworkPacketArgs

var _ stack.NetworkDispatcher = (*dispatcherChan)(nil)

func (ch *dispatcherChan) DeliverNetworkPacket(_ stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView, linkHeader buffer.View) {
	*ch <- deliverNetworkPacketArgs{
		srcLinkAddr: srcLinkAddr,
		dstLinkAddr: dstLinkAddr,
		protocol:    protocol,
		vv:          vv,
		linkHeader:  linkHeader,
	}
}

func TestEndpoint_WritePacket(t *testing.T) {
	var outFifo, inFifo zx.Handle
	if status := zx.Sys_fifo_create(1, eth.FifoEntrySize, 0, &outFifo, &inFifo); status != zx.ErrOk {
		t.Fatal(status)
	}

	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	arena.TestOnlyDisableOwnerCheck = true

	baseDevice := ethernetext.Device{
		TB: t,
		GetInfoImpl: func() (ethernet.Info, error) {
			return ethernet.Info{}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		SetClientNameImpl: func(string) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}

	outDevice := baseDevice
	outDevice.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
		return int32(zx.ErrOk), &ethernet.Fifos{Tx: outFifo, TxDepth: 1}, nil
	}
	outClient, err := eth.NewClient(t.Name(), "out", &outDevice, arena)
	if err != nil {
		t.Fatal(err)
	}
	outEndpoint := eth.NewLinkEndpoint(outClient)

	inDevice := baseDevice
	inDevice.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
		return int32(zx.ErrOk), &ethernet.Fifos{Rx: inFifo, RxDepth: 1}, nil
	}
	inClient, err := eth.NewClient(t.Name(), "in", &inDevice, arena)
	if err != nil {
		t.Fatal(err)
	}
	inEndpoint := eth.NewLinkEndpoint(inClient)

	ch := make(dispatcherChan, 1)
	inEndpoint.Attach(&ch)

	// Test that the ethernet frame is built correctly.
	const localLinkAddress = tcpip.LinkAddress("\x01\x02\x03\x04\x05\x06")
	const remoteLinkAddress = tcpip.LinkAddress("\x11\x12\x13\x14\x15\x16")
	// Test that unused bytes are not accidentally put on the wire.
	const header = "foo"
	hdr := buffer.NewPrependable(10)
	if want, got := len(header), copy(hdr.Prepend(len(header)), header); got != want {
		t.Fatalf("got copy() = %d, want = %d", got, want)
	}
	const body = "bar"
	payload := buffer.NewViewFromBytes([]byte(body)).ToVectorisedView()
	const protocol = tcpip.NetworkProtocolNumber(45)
	if err := outEndpoint.WritePacket(&stack.Route{
		LocalLinkAddress:  localLinkAddress,
		RemoteLinkAddress: remoteLinkAddress,
	}, nil, hdr, payload, protocol); err != nil {
		t.Fatal(err)
	}
	select {
	case <-time.After(5 * time.Second):
		t.Fatal("timeout waiting for ethernet packet")
	case args := <-ch:
		if want, got := localLinkAddress, args.srcLinkAddr; got != want {
			t.Errorf("got srcLinkAddr = %s, want = %s", got, want)
		}
		if want, got := remoteLinkAddress, args.dstLinkAddr; got != want {
			t.Errorf("got dstLinkAddr = %s, want = %s", got, want)
		}
		if want, got := protocol, args.protocol; got != want {
			t.Errorf("got protocol = %d, want = %d", got, want)
		}
		if want, got := []byte(header+body), args.vv.ToView(); !bytes.Equal(got, want) {
			t.Errorf("got vv = %x, want = %x", got, want)
		}
	}
}

// TestEndpoint_ReceivePacket tests that receiving ethernet frames of size
// less than the minimum size does not panic or cause any issues for future
// (valid) frames.
func TestEndpoint_ReceivePacket(t *testing.T) {
	var outFifo, inFifo zx.Handle
	if status := zx.Sys_fifo_create(1, eth.FifoEntrySize, 0, &outFifo, &inFifo); status != zx.ErrOk {
		t.Fatal(status)
	}

	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	arena.TestOnlyDisableOwnerCheck = true

	baseDevice := ethernetext.Device{
		TB: t,
		GetInfoImpl: func() (ethernet.Info, error) {
			return ethernet.Info{}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		SetClientNameImpl: func(string) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}

	inDevice := baseDevice
	inDevice.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
		return int32(zx.ErrOk), &ethernet.Fifos{Rx: inFifo, RxDepth: 1}, nil
	}
	inClient, err := eth.NewClient(t.Name(), "in", &inDevice, arena)
	if err != nil {
		t.Fatal(err)
	}
	inEndpoint := eth.NewLinkEndpoint(inClient)

	outDevice := baseDevice
	outDevice.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
		return int32(zx.ErrOk), &ethernet.Fifos{Tx: outFifo, TxDepth: 1}, nil
	}
	outClient, err := eth.NewClient(t.Name(), "out", &outDevice, arena)
	if err != nil {
		t.Fatal(err)
	}

	ch := make(dispatcherChan, 1)
	inEndpoint.Attach(&ch)

	const localLinkAddress = tcpip.LinkAddress("\x01\x02\x03\x04\x05\x06")
	const remoteLinkAddress = tcpip.LinkAddress("\x11\x12\x13\x14\x15\x16")
	const headerBuf = "foo"
	const bodyBuf = "bar"
	const protocol = tcpip.NetworkProtocolNumber(45)
	lenHeaderBuf := len(headerBuf)
	lenBodyBuf := len(bodyBuf)
	ethHdr := header.EthernetFields{
		SrcAddr: localLinkAddress,
		DstAddr: remoteLinkAddress,
		Type:    protocol,
	}
	ethHeaderBuf := make([]byte, header.EthernetMinimumSize)
	header.Ethernet(ethHeaderBuf).Encode(&ethHdr)

	// Send the first sendSize bytes of a frame from outClient to inClient.
	send := func(sendSize int) {
		t.Helper()

		var buf eth.Buffer
		for {
			if buf = outClient.AllocForSend(); buf != nil {
				break
			}
			if err := outClient.WaitSend(); err != nil {
				t.Fatal(err)
			}
		}
		if copied := copy(buf, ethHeaderBuf); copied != header.EthernetMinimumSize {
			t.Fatalf("got copy(_, ethHeaderBuf) = %d, want = %d", copied, header.EthernetMinimumSize)
		}
		used := header.EthernetMinimumSize
		if copied := copy(buf[used:], headerBuf); copied != lenHeaderBuf {
			t.Fatalf("got copy(_, headerBuf) = %d, want = %d", copied, lenHeaderBuf)
		}
		used += lenHeaderBuf
		if copied := copy(buf[used:], bodyBuf); copied != lenBodyBuf {
			t.Fatalf("got copy(_, bodyBuf) = %d, want = %d", copied, lenBodyBuf)
		}
		used += lenBodyBuf

		if err := outClient.Send(buf[:sendSize]); err != nil {
			t.Fatal(err)
		}
	}

	// Wait for a packet to be delivered on ch and validate the delivered
	// network packet parameters. The packet should be delivered within 5s.
	expectedBuf := func(expectedBuf []byte) {
		t.Helper()

		select {
		case <-time.After(5 * time.Second):
			t.Fatal("timeout waiting for ethernet packet")
		case args := <-ch:
			if want, got := localLinkAddress, args.srcLinkAddr; got != want {
				t.Fatalf("got srcLinkAddr = %s, want = %s", got, want)
			}
			if want, got := remoteLinkAddress, args.dstLinkAddr; got != want {
				t.Fatalf("got dstLinkAddr = %s, want = %s", got, want)
			}
			if want, got := protocol, args.protocol; got != want {
				t.Fatalf("got protocol = %d, want = %d", got, want)
			}
			if got := args.vv.ToView(); !bytes.Equal(got, expectedBuf) {
				t.Fatalf("got vv = %x, want = %x", got, expectedBuf)
			}
			if got := args.linkHeader; !bytes.Equal(got, ethHeaderBuf) {
				t.Fatalf("got linkHeader = %x, want = %x", got, ethHeaderBuf)
			}
		}
	}

	// Test receiving a frame that is too small.
	send(header.EthernetMinimumSize - 1)
	select {
	case <-time.After(time.Second):
	case <-ch:
		t.Fatal("should not have delivered a packet for a frame that is too small")
	}

	// Test receiving a frame that is equal to the minimum frame size.
	send(header.EthernetMinimumSize)
	expectedBuf([]byte{})

	// Test receiving a frame that is just greater than the minimum frame
	// size.
	send(header.EthernetMinimumSize + 1)
	expectedBuf([]byte{headerBuf[0]})

	// Test receiving the normal frame.
	send(header.EthernetMinimumSize + lenHeaderBuf + lenBodyBuf)
	expectedBuf([]byte(headerBuf + bodyBuf))
}
