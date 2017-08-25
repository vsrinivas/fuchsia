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

	"apps/netstack/services/net_address"
	nsfidl "apps/netstack/services/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct{}

func toNetAddress(addr tcpip.Address) net_address.NetAddress {
	out := net_address.NetAddress{Family: net_address.NetAddressFamily_Unspecified}
	switch len(addr) {
	case 4:
		out.Family = net_address.NetAddressFamily_Ipv4
		out.Ipv4 = &[4]uint8{}
		copy(out.Ipv4[:], addr[:])
	case 16:
		out.Family = net_address.NetAddressFamily_Ipv6
		out.Ipv6 = &[16]uint8{}
		copy(out.Ipv6[:], addr[:])
	}
	return out
}

func toSubnets(addrs []tcpip.Address) []net_address.Subnet {
	out := make([]net_address.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = net_address.Subnet{Addr: toNetAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func (ni *netstackImpl) GetPortForService(service string, protocol nsfidl.Protocol) (port uint16, err error) {
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

func (ni *netstackImpl) GetAddress(name string, port uint16) (out []net_address.SocketAddress, err error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// mxio's getaddrinfo into here.
	addrs, err := ns.dispatcher.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]net_address.SocketAddress, len(addrs))
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

func (ni *netstackImpl) GetInterfaces() (out []nsfidl.NetInterface, err error) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	index := uint32(0)
	for nicid, ifs := range ns.ifStates {
		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}

		// TODO: set flags based on actual link status. NET-134
		outif := nsfidl.NetInterface{
			Id:        uint32(nicid),
			Flags:     nsfidl.NetInterfaceFlagUp,
			Name:      fmt.Sprintf("en%d", nicid),
			Addr:      toNetAddress(ifs.nic.Addr),
			Netmask:   toNetAddress(tcpip.Address(ifs.nic.Netmask)),
			Broadaddr: toNetAddress(tcpip.Address(broadaddr)),
			Hwaddr:    []uint8(ifs.nic.Mac[:]),
			Ipv6addrs: toSubnets(ifs.nic.Ipv6addrs),
		}

		out = append(out, outif)
		index++
	}
	return out, nil
}

func (ni *netstackImpl) GetNodeName() (out string, err error) {
	ns.mu.Lock()
	nodename := ns.nodename
	ns.mu.Unlock()
	return nodename, nil
}

type netstackDelegate struct {
	stubs []*bindings.Stub
}

func (delegate *netstackDelegate) Bind(request nsfidl.Netstack_Request) {
	stub := request.NewStub(&netstackImpl{}, bindings.GetAsyncWaiter())
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

func (delegate *netstackDelegate) Quit() {
	for _, stub := range delegate.stubs {
		stub.Close()
	}
}

// AddNetstackService registers the NetstackService with the application context,
// allowing it to respond to FIDL queries.
func AddNetstackService(ctx *context.Context) {
	ctx.OutgoingService.AddService(&nsfidl.Netstack_ServiceBinder{&netstackDelegate{}})
}
