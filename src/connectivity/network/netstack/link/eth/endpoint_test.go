// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"bytes"
	"runtime"
	"syscall/zx"
	"testing"

	"netstack/link/eth"

	"fidl/fuchsia/hardware/ethernet"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

var _ stack.NetworkDispatcher = dispatcherFunc(nil)

type dispatcherFunc func(stack.LinkEndpoint, tcpip.LinkAddress, tcpip.LinkAddress, tcpip.NetworkProtocolNumber, buffer.VectorisedView)

func (f dispatcherFunc) DeliverNetworkPacket(linkEP stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	f(linkEP, srcLinkAddr, dstLinkAddr, protocol, vv)
}

func TestEndpoint_WritePacket(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}

	d := ethernetext.Device{
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
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{}, nil
		},
	}
	c, err := eth.NewClient(t.Name(), "topo", &d, arena)
	if err != nil {
		t.Fatal(err)
	}
	e := eth.NewLinkEndpoint(c)
	var packetsDelivered int
	var dstLinkAddrDelivered, srcLinkAddrDelivered tcpip.LinkAddress
	var protocolDelivered tcpip.NetworkProtocolNumber
	var vvDelivered buffer.VectorisedView
	e.Attach(dispatcherFunc(func(linkEP stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
		packetsDelivered++
		dstLinkAddrDelivered = dstLinkAddr
		srcLinkAddrDelivered = srcLinkAddr
		protocolDelivered = protocol
		vvDelivered = vv
	}))
	const address = "\xff\xff\xff\xff"

	// Test that the ethernet frame is built correctly.
	const localLinkAddress = tcpip.LinkAddress("\x01\x02\x03\x04\x05\x06")
	const remoteLinkAddress = tcpip.LinkAddress("\x11\x12\x13\x14\x15\x16")
	// Only 3 of the 10 bytes are used to check that unused bytes are
	// not accidentally put on the wire.
	hdr := buffer.NewPrependable(10)
	copy(hdr.Prepend(3), "foo")
	payload := buffer.NewViewFromBytes([]byte("bar")).ToVectorisedView()
	const fakeProtocolNumber = 45
	const protocol = tcpip.NetworkProtocolNumber(fakeProtocolNumber)
	if err := e.WritePacket(&stack.Route{
		LocalAddress:      address,
		RemoteAddress:     address,
		LocalLinkAddress:  localLinkAddress,
		RemoteLinkAddress: remoteLinkAddress,
	}, nil, hdr, payload, protocol); err != nil {
		t.Fatal(err)
	}

	if want := 1; packetsDelivered != want {
		t.Errorf("got %d packets delivered, want = %d", packetsDelivered, want)
	}
	if want := remoteLinkAddress; srcLinkAddrDelivered != want {
		t.Errorf("got srcLinkAddrDelivered = %#v, want = %#v", srcLinkAddrDelivered, want)
	}
	if want := localLinkAddress; dstLinkAddrDelivered != want {
		t.Errorf("got dstLinkAddrDelivered = %#v, want = %#v", dstLinkAddrDelivered, want)
	}
	if want := protocol; protocolDelivered != want {
		t.Errorf("got protocolDelivered = %#v, want = %#v", protocolDelivered, want)
	}
	if want := buffer.NewViewFromBytes([]byte("foobar")); !bytes.Equal(vvDelivered.ToView(), want) {
		t.Errorf("got vvDelivered.ToView() = %s, want = %s", vvDelivered.ToView(), want)
	}
	// We've created a driver with bogus fifos; the loop servicing them
	// might crash. Give it a chance to run to make such crashes
	// deterministic.
	runtime.Gosched()
}
