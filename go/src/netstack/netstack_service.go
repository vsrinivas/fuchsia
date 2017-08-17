// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"

	"application/lib/app/context"

	"fidl/bindings"

	"syscall/mx"
	"syscall/mx/mxerror"

	nsaddr "apps/netstack/services/net_address"
	nsfidl "apps/netstack/services/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type NetstackImpl struct{}

func toNetAddress(addr tcpip.Address) nsaddr.NetAddress {
	out := nsaddr.NetAddress{Family: nsaddr.NetAddressFamily_Unspecified}
	switch len(addr) {
	case 4:
		out.Family = nsaddr.NetAddressFamily_Ipv4
		out.Ipv4 = &[4]uint8{}
		copy(out.Ipv4[:], addr[:])
	case 16:
		out.Family = nsaddr.NetAddressFamily_Ipv6
		out.Ipv6 = &[16]uint8{}
		copy(out.Ipv6[:], addr[:])
	}
	return out
}

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

func (ni *NetstackImpl) GetAddress(name string, port uint16) (out []nsaddr.SocketAddress, err error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// mxio's getaddrinfo into here.
	addrs, err := ns.dispatcher.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]nsaddr.SocketAddress, len(addrs))
		for i, addr := range addrs {
			switch len(addr) {
			case 4, 16:
				out[i].Addr = toNetAddress(addr)
				out[i].Port = port
			}
		}
	}
	return out, err
}

func (ni *NetstackImpl) GetInterfaces() (out []nsfidl.NetInterface, err error) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	index := uint32(0)
	for nicid, netif := range ns.netifs {
		if netif.addr == header.IPv4Loopback {
			continue
		}

		// Long-hand for: broadaddr = netif.addr | ^netif.netmask
		broadaddr := []byte(netif.addr)
		for i := range broadaddr {
			broadaddr[i] |= ^netif.netmask[i]
		}

		// TODO: set flags based on actual link status. NET-134
		outif := nsfidl.NetInterface{
			Id:        uint32(nicid),
			Flags:     nsfidl.NetInterfaceFlagUp,
			Name:      fmt.Sprintf("en%d", nicid),
			Addr:      toNetAddress(netif.addr),
			Netmask:   toNetAddress(tcpip.Address(netif.netmask)),
			Broadaddr: toNetAddress(tcpip.Address(broadaddr)),
		}

		out = append(out, outif)
		index++
	}
	return out, nil
}

func (ni *NetstackImpl) GetNodeName() (out string, err error) {
	ns.mu.Lock()
	nodename := ns.nodename
	ns.mu.Unlock()
	return nodename, nil
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
