// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"syscall/zx"
	"testing"

	"netstack/link/eth"

	"fidl/zircon/ethernet"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

var _ stack.NetworkDispatcher = dispatcherFunc(nil)

type dispatcherFunc func(stack.LinkEndpoint, tcpip.LinkAddress, tcpip.LinkAddress, tcpip.NetworkProtocolNumber, buffer.VectorisedView)

func (f dispatcherFunc) DeliverNetworkPacket(linkEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	f(linkEP, dstLinkAddr, srcLinkAddr, protocol, vv)
}

func TestEndpoint_WritePacket(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}

	d := device{
		TB: t,
		getInfo: func() (ethernet.Info, error) {
			return ethernet.Info{}, nil
		},
		setIoBuffer: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		start: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		setClientName: func(string) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		getFifos: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				Rx: zx.HandleInvalid,
				Tx: zx.HandleInvalid,
			}, nil
		},
	}
	c, err := eth.NewClient(t.Name(), "topo", &d, arena, nil)
	if err != nil {
		t.Fatal(err)
	}
	e := eth.NewLinkEndpoint(c)
	var packetsDelivered int
	e.Attach(dispatcherFunc(func(linkEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
		packetsDelivered++
	}))
	const address = "\xff\xff\xff\xff"
	e.WritePacket(&stack.Route{
		LocalAddress:  address,
		RemoteAddress: address,
	}, buffer.Prependable{}, buffer.VectorisedView{}, 0)
	if want := 1; packetsDelivered != want {
		t.Errorf("got %d packets delivered, want = %d", packetsDelivered, want)
	}
}
