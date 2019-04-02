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
	"github.com/google/netstack/tcpip/stack"
)

type deliverNetworkPacketArgs struct {
	srcLinkAddr, dstLinkAddr tcpip.LinkAddress
	protocol                 tcpip.NetworkProtocolNumber
	vv                       buffer.VectorisedView
}

type dispatcherChan chan deliverNetworkPacketArgs

var _ stack.NetworkDispatcher = (*dispatcherChan)(nil)

func (ch *dispatcherChan) DeliverNetworkPacket(_ stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	*ch <- deliverNetworkPacketArgs{
		srcLinkAddr: srcLinkAddr,
		dstLinkAddr: dstLinkAddr,
		protocol:    protocol,
		vv:          vv,
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
			t.Errorf("got srcLinkAddr = %+v, want = %+v", got, want)
		}
		if want, got := remoteLinkAddress, args.dstLinkAddr; got != want {
			t.Errorf("got dstLinkAddr = %+v, want = %+v", got, want)
		}
		if want, got := protocol, args.protocol; got != want {
			t.Errorf("got protocol = %d, want = %d", got, want)
		}
		if want, got := []byte(header+body), args.vv.ToView(); !bytes.Equal(got, want) {
			t.Errorf("got vv = %x, want = %x", got, want)
		}
	}
}
