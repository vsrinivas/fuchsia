// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"

	"application/lib/app/context"

	"fidl/bindings"

	"syscall/mx"
	"syscall/mx/mxerror"

	nsfidl "apps/netstack/services/netstack"
	nsaddr "apps/netstack/services/net_address"

	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type NetstackImpl struct{}

func (ni *NetstackImpl) GetPortForService(service string, protocol nsfidl.Protocol) (port uint16, err error) {
	switch protocol {
	case nsfidl.Protocol_Udp:
		port, err = serviceLookup(service, udp.ProtocolNumber)
	case nsfidl.Protocol_Tcp:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
	default:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
		if err != nil {
			port, err = serviceLookup(service, udp.ProtocolNumber)
		}
	}
	return port, err
}

func (ni *NetstackImpl) GetAddress(name string, port uint16) (out []nsaddr.NetAddress, err error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// mxio's getaddrinfo into here.
	addrs, err := ns.dispatcher.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]nsaddr.NetAddress, len(addrs))
		for i, addr := range addrs {
			switch len(addr) {
			case 0:
				// skip
			case 4:
				out[i].Family = nsaddr.NetAddressFamily_Ipv4
				out[i].Ipv4 = &nsaddr.NetAddressIPv4{Port: port}
				copy(out[i].Ipv4.Addr[:], addr[:])
			case 16:
				out[i].Family = nsaddr.NetAddressFamily_Ipv6
				out[i].Ipv6 = &nsaddr.NetAddressIPv6{Port: port}
				copy(out[i].Ipv6.Addr[:], addr[:])
			}
		}
	}
	return out, err
}

type NetstackDelegate struct {
	stubs []*bindings.Stub
}

func (delegate *NetstackDelegate) Bind(request nsfidl.Netstack_Request) {
	stub := request.NewStub(&NetstackImpl{}, bindings.GetAsyncWaiter())
	delegate.stubs = append(delegate.stubs, stub)
	go func() {
		for {
			if err := stub.ServeRequest(); err != nil {
				if mxerror.Status(err) != mx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func (delegate *NetstackDelegate) Quit() {
	for _, stub := range delegate.stubs {
		stub.Close()
	}
}

func AddNetstackService(ctx *context.Context) {
	ctx.OutgoingService.AddService(&nsfidl.Netstack_ServiceBinder{&NetstackDelegate{}})
}
