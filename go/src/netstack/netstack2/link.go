// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"syscall/mx"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

const headerLength = 14

type linkEndpoint struct {
	ch       *mx.Channel
	mtu      uint32
	linkAddr tcpip.LinkAddress

	vv    buffer.VectorisedView
	views [1]buffer.View
}

func (ep *linkEndpoint) MTU() uint32                    { return ep.mtu }
func (ep *linkEndpoint) MaxHeaderLength() uint16        { return headerLength }
func (ep *linkEndpoint) LinkAddress() tcpip.LinkAddress { return ep.linkAddr }

func (ep *linkEndpoint) WritePacket(r *stack.Route, hdr *buffer.Prependable, payload buffer.View, protocol tcpip.NetworkProtocolNumber) error {
	ethHdr := hdr.Prepend(headerLength)
	if r.RemoteLinkAddress == "" && r.RemoteAddress == "\xff\xff\xff\xff" {
		r.RemoteLinkAddress = "\xff\xff\xff\xff\xff\xff"
	}
	copy(ethHdr[0:], r.RemoteLinkAddress)
	copy(ethHdr[6:], ep.linkAddr)
	ethHdr[12] = uint8(protocol >> 8)
	ethHdr[13] = uint8(protocol)

	pktlen := len(hdr.UsedBytes()) + len(payload)
	if pktlen < 60 {
		pktlen = 60
	}
	b := make([]byte, pktlen)
	copy(b, hdr.UsedBytes())
	copy(b[len(hdr.UsedBytes()):], payload)
	err := ep.ch.Write(b, nil, 0)
	return err
}

func (ep *linkEndpoint) Attach(dispatcher stack.NetworkDispatcher) {
	go func() {
		for {
			if err := ep.dispatch(dispatcher); err != nil {
				log.Printf("dispatch error: %v", err)
				return
			}
		}
	}()
}

func (ep *linkEndpoint) dispatch(d stack.NetworkDispatcher) error {
	v := make([]byte, ep.mtu)
	n, err := ep.read(v)
	if err != nil {
		return err
	}
	ep.views[0] = buffer.View(v)
	ep.vv.SetViews(ep.views[:])
	ep.vv.SetSize(n)

	remoteLinkAddr := tcpip.LinkAddress(v[6:12])
	p := tcpip.NetworkProtocolNumber(uint16(v[12])<<8 | uint16(v[13]))

	ep.vv.TrimFront(headerLength)
	d.DeliverNetworkPacket(ep, remoteLinkAddr, p, &ep.vv)

	return nil
}

func (ep *linkEndpoint) read(data []byte) (int, error) {
	for {
		n, _, err := ep.ch.Read(data, nil, 0)
		if err == mx.ErrShouldWait {
			sig, err := ep.ch.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)
			if err != nil {
				return 0, err
			}
			if sig&mx.SignalChannelPeerClosed != 0 {
				return 0, fmt.Errorf("netstack: read handle closed")
			}
			continue
		}
		return int(n), err
	}
}

func (ep *linkEndpoint) init() error {
	var b [8]byte
	n, err := ep.read(b[:])
	if err != nil {
		return err
	}
	if n < 8 {
		return fmt.Errorf("netstack: short initial read: n=%d\n", n)
	}
	ep.linkAddr = tcpip.LinkAddress(b[:6])
	log.Printf("linkaddr: %x:%x:%x:%x:%x:%x", b[0], b[1], b[2], b[3], b[4], b[5])
	return nil
}

func newLinkEndpoint(ch *mx.Channel) *linkEndpoint {
	ep := &linkEndpoint{
		ch:  ch,
		mtu: 2048,
	}
	return ep
}
