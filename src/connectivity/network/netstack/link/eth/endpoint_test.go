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

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

type DeliverNetworkPacketArgs struct {
	SrcLinkAddr, DstLinkAddr tcpip.LinkAddress
	Protocol                 tcpip.NetworkProtocolNumber
	Pkt                      tcpip.PacketBuffer
}

type dispatcherChan chan DeliverNetworkPacketArgs

var _ stack.NetworkDispatcher = (*dispatcherChan)(nil)

func (ch *dispatcherChan) DeliverNetworkPacket(_ stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) {
	*ch <- DeliverNetworkPacketArgs{
		SrcLinkAddr: srcLinkAddr,
		DstLinkAddr: dstLinkAddr,
		Protocol:    protocol,
		Pkt:         pkt,
	}
}

func vectorizedViewComparer(x, y buffer.VectorisedView) bool {
	return bytes.Equal(x.ToView(), y.ToView())
}

func prependableComparer(x, y buffer.Prependable) bool {
	return bytes.Equal(x.View(), y.View())
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

	// Test that we build the ethernet frame correctly.
	const localLinkAddress = tcpip.LinkAddress("\x01\x02\x03\x04\x05\x06")
	const remoteLinkAddress = tcpip.LinkAddress("\x11\x12\x13\x14\x15\x16")
	// Test that we don't accidentally put unused bytes on the wire.
	const header = "foo"
	hdr := buffer.NewPrependable(len(header) + 5)
	if want, got := len(header), copy(hdr.Prepend(len(header)), header); got != want {
		t.Fatalf("got copy() = %d, want = %d", got, want)
	}
	const body = "bar"
	const protocol = tcpip.NetworkProtocolNumber(45)
	if err := outEndpoint.WritePacket(
		&stack.Route{
			LocalLinkAddress:  localLinkAddress,
			RemoteLinkAddress: remoteLinkAddress,
		},
		nil,
		protocol,
		tcpip.PacketBuffer{
			Data:   buffer.View(body).ToVectorisedView(),
			Header: hdr,
		},
	); err != nil {
		t.Fatal(err)
	}
	select {
	case <-time.After(5 * time.Second):
		t.Fatal("timeout waiting for ethernet packet")
	case args := <-ch:
		if diff := cmp.Diff(args, DeliverNetworkPacketArgs{
			SrcLinkAddr: localLinkAddress,
			DstLinkAddr: remoteLinkAddress,
			Protocol:    protocol,
			Pkt: tcpip.PacketBuffer{
				Data: buffer.View(header + body).ToVectorisedView(),
			},
		}, cmp.Comparer(vectorizedViewComparer), cmp.Comparer(prependableComparer)); diff != "" {
			t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
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
	const protocol = tcpip.NetworkProtocolNumber(45)
	const payload = "foobarbaz"
	var headerBuffer [header.EthernetMinimumSize]byte
	header.Ethernet(headerBuffer[:]).Encode(&header.EthernetFields{
		SrcAddr: localLinkAddress,
		DstAddr: remoteLinkAddress,
		Type:    protocol,
	})

	// Send the first sendSize bytes of a frame from outClient to inClient.
	send := func(sendSize int) {
		var buf eth.Buffer
		for {
			if buf = outClient.AllocForSend(); buf != nil {
				break
			}
			if err := outClient.WaitSend(); err != nil {
				t.Fatal(err)
			}
		}
		used := 0
		used += copy(buf[used:], headerBuffer[:])
		used += copy(buf[used:], payload)
		if want := header.EthernetMinimumSize + len(payload); used != want {
			t.Fatalf("got used = %d, want %d", used, want)
		}

		if err := outClient.Send(buf[:sendSize]); err != nil {
			t.Fatal(err)
		}
	}

	// Test receiving a frame that is too small.
	send(header.EthernetMinimumSize - 1)
	select {
	case <-time.After(time.Second):
	case args := <-ch:
		t.Fatalf("unexpected packet received: %+v", args)
	}

	for _, extra := range []int{
		// Test receiving a frame that is equal to the minimum frame size.
		0,
		// Test receiving a frame that is just greater than the minimum frame size.
		1,
		// Test receiving the full frame.
		len(payload),
	} {
		send(header.EthernetMinimumSize + extra)

		// Wait for a packet to be delivered on ch and validate the delivered
		// network packet parameters. The packet should be delivered within 5s.
		select {
		case <-time.After(5 * time.Second):
			t.Fatal("timeout waiting for ethernet packet")
		case args := <-ch:
			if diff := cmp.Diff(args, DeliverNetworkPacketArgs{
				SrcLinkAddr: localLinkAddress,
				DstLinkAddr: remoteLinkAddress,
				Protocol:    protocol,
				Pkt: tcpip.PacketBuffer{
					Data: buffer.View(payload[:extra]).ToVectorisedView(),
				},
			}, cmp.Comparer(vectorizedViewComparer), cmp.Comparer(prependableComparer)); diff != "" {
				t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
			}
		}
	}
}
