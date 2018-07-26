// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"sort"
	"strings"

	"app/context"
	"fidl/bindings"
	"netstack/connectivity"
	"netstack/fidlconv"
	"netstack/link/eth"
	"syscall/zx"

	"fidl/fuchsia/net"
	nsfidl "fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct {
	listener nsfidl.NotificationListenerInterface
}

func toSubnets(addrs []tcpip.Address) []nsfidl.Subnet {
	out := make([]nsfidl.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = nsfidl.Subnet{Addr: fidlconv.ToNetAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func getInterfaces() (out []nsfidl.NetInterface) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		if len(ifs.nic.Netmask) != len(ifs.nic.Addr) {
			log.Printf("warning: mismatched netmask and address length for nic: %+v", ifs.nic)
			continue
		}

		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}

		var flags uint32
		if ifs.state == eth.StateStarted {
			flags |= nsfidl.NetInterfaceFlagUp
		}
		if ifs.dhcpState.enabled {
			flags |= nsfidl.NetInterfaceFlagDhcp
		}

		outif := nsfidl.NetInterface{
			Id:        uint32(nicid),
			Flags:     flags,
			Features:  ifs.nic.Features,
			Name:      ifs.nic.Name,
			Addr:      fidlconv.ToNetAddress(ifs.nic.Addr),
			Netmask:   fidlconv.ToNetAddress(tcpip.Address(ifs.nic.Netmask)),
			Broadaddr: fidlconv.ToNetAddress(tcpip.Address(broadaddr)),
			Hwaddr:    []uint8(ifs.nic.Mac[:]),
			Ipv6addrs: toSubnets(ifs.nic.Ipv6addrs),
		}

		out = append(out, outif)
	}
	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ni *netstackImpl) RegisterListener(listener nsfidl.NotificationListenerInterface) (err error) {
	if bindings.Proxy(listener).IsValid() {
		ni.listener = listener
	}
	return nil
}

func (ni *netstackImpl) GetPortForService(service string, protocol nsfidl.Protocol) (port uint16, err error) {
	switch protocol {
	case nsfidl.ProtocolUdp:
		port, err = serviceLookup(service, udp.ProtocolNumber)
	case nsfidl.ProtocolTcp:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
	default:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
		if err != nil {
			port, err = serviceLookup(service, udp.ProtocolNumber)
		}
	}
	return port, err
}

func (ni *netstackImpl) GetAddress(name string, port uint16) (out []nsfidl.SocketAddress, netErr nsfidl.NetErr, retErr error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// fdio's getaddrinfo into here.
	addrs, err := ns.dispatcher.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]nsfidl.SocketAddress, len(addrs))
		netErr = nsfidl.NetErr{Status: nsfidl.StatusOk}
		for i, addr := range addrs {
			switch len(addr) {
			case 4, 16:
				out[i].Addr = fidlconv.ToNetAddress(addr)
				out[i].Port = port
			}
		}
	} else {
		netErr = nsfidl.NetErr{Status: nsfidl.StatusDnsError, Message: err.Error()}
	}
	return out, netErr, nil
}

func (ni *netstackImpl) GetInterfaces() (out []nsfidl.NetInterface, err error) {
	return getInterfaces(), nil
}

func (ni *netstackImpl) GetRouteTable() (out []nsfidl.RouteTableEntry, err error) {
	ns.mu.Lock()
	table := ns.stack.GetRouteTable()
	ns.mu.Unlock()

	for _, route := range table {
		// Ensure that if any of the returned addresss are "empty",
		// they still have the appropriate NetAddressFamily.
		l := 0
		if len(route.Destination) > 0 {
			l = len(route.Destination)
		} else if len(route.Mask) > 0 {
			l = len(route.Destination)
		} else if len(route.Gateway) > 0 {
			l = len(route.Gateway)
		}
		dest := route.Destination
		mask := route.Mask
		gateway := route.Gateway
		if len(dest) == 0 {
			dest = tcpip.Address(strings.Repeat("\x00", l))
		}
		if len(mask) == 0 {
			mask = tcpip.Address(strings.Repeat("\x00", l))
		}
		if len(gateway) == 0 {
			gateway = tcpip.Address(strings.Repeat("\x00", l))
		}

		out = append(out, nsfidl.RouteTableEntry{
			Destination: fidlconv.ToNetAddress(dest),
			Netmask:     fidlconv.ToNetAddress(mask),
			Gateway:     fidlconv.ToNetAddress(gateway),
			Nicid:       uint32(route.NIC),
		})
	}
	return out, nil
}

func (ni *netstackImpl) SetRouteTable(rt []nsfidl.RouteTableEntry) error {
	routes := []tcpip.Route{}
	for _, r := range rt {
		route := tcpip.Route{
			Destination: fidlconv.NetAddressToTCPIPAddress(r.Destination),
			Mask:        fidlconv.NetAddressToTCPIPAddress(r.Netmask),
			Gateway:     fidlconv.NetAddressToTCPIPAddress(r.Gateway),
			NIC:         tcpip.NICID(r.Nicid),
		}
		routes = append(routes, route)
	}

	ns.mu.Lock()
	defer ns.mu.Unlock()
	ns.stack.SetRouteTable(routes)

	return nil
}

func toSubnet(address nsfidl.NetAddress, prefixLen uint8) (tcpip.Subnet, error) {
	// TODO: use tcpip.Address#mask after fuchsia/third_party/netstack and github/third_party/netstack are unified and #mask can be made public.
	a := []byte(fidlconv.NetAddressToTCPIPAddress(address))
	m := tcpip.CIDRMask(int(prefixLen), int(len(a)*8))
	for i, _ := range a {
		a[i] = a[i] & m[i]
	}
	return tcpip.NewSubnet(tcpip.Address(a), m)
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(nicid uint32, address nsfidl.NetAddress, prefixLen uint8) (result nsfidl.NetErr, endService error) {
	log.Printf("net address %+v", address)
	var protocol tcpip.NetworkProtocolNumber
	switch address.Family {
	case nsfidl.NetAddressFamilyIpv4:
		protocol = ipv4.ProtocolNumber
	case nsfidl.NetAddressFamilyIpv6:
		return nsfidl.NetErr{nsfidl.StatusIpv4Only, "IPv6 not yet supported for SetInterfaceAddress"}, nil
	}

	nic := tcpip.NICID(nicid)
	addr := fidlconv.NetAddressToTCPIPAddress(address)
	sn, err := toSubnet(address, prefixLen)
	if err != nil {
		result = nsfidl.NetErr{nsfidl.StatusParseError, "Error applying subnet mask to interface address"}
		return result, nil
	}

	if err = ns.setInterfaceAddress(nic, protocol, addr, sn); err != nil {
		return nsfidl.NetErr{nsfidl.StatusUnknownError, err.Error()}, nil
	}
	return nsfidl.NetErr{nsfidl.StatusOk, ""}, nil
}

func (ni *netstackImpl) BridgeInterfaces(nicids []uint32) (nsfidl.NetErr, error) {
	nics := make([]tcpip.NICID, len(nicids))
	for i, n := range nicids {
		nics[i] = tcpip.NICID(n)
	}
	err := ns.Bridge(nics)
	if err != nil {
		return nsfidl.NetErr{Status: nsfidl.StatusUnknownError}, nil
	}
	return nsfidl.NetErr{Status: nsfidl.StatusOk}, nil
}

func (ni *netstackImpl) SetFilterStatus(enabled bool) (result nsfidl.NetErr, err error) {
	ns.filter.Enable(enabled)
	return nsfidl.NetErr{nsfidl.StatusOk, ""}, nil
}

func (ni *netstackImpl) GetFilterStatus() (enabled bool, err error) {
	return ns.filter.IsEnabled(), nil
}

func (ni *netstackImpl) GetAggregateStats() (stats nsfidl.AggregateStats, err error) {
	s := ns.stack.Stats()
	return nsfidl.AggregateStats{
		UnknownProtocolReceivedPackets: s.UnknownProtocolRcvdPackets,
		MalformedReceivedPackets:       s.MalformedRcvdPackets,
		DroppedPackets:                 s.DroppedPackets,
		IpStats: nsfidl.IpStats{
			PacketsReceived:          s.IP.PacketsReceived,
			InvalidAddressesReceived: s.IP.InvalidAddressesReceived,
			PacketsDiscarded:         s.IP.PacketsDiscarded,
			PacketsDelivered:         s.IP.PacketsDelivered,
			PacketsSent:              s.IP.PacketsSent,
			OutgoingPacketErrors:     s.IP.OutgoingPacketErrors,
		},
		TcpStats: nsfidl.TcpStats{
			ActiveConnectionOpenings:  s.TCP.ActiveConnectionOpenings,
			PassiveConnectionOpenings: s.TCP.PassiveConnectionOpenings,
			FailedConnectionAttempts:  s.TCP.FailedConnectionAttempts,
			ValidSegmentsReceived:     s.TCP.ValidSegmentsReceived,
			InvalidSegmentsReceived:   s.TCP.InvalidSegmentsReceived,
			SegmentsSent:              s.TCP.SegmentsSent,
			ResetsSent:                s.TCP.ResetsSent,
		},
		UdpStats: nsfidl.UdpStats{
			PacketsReceived:          s.UDP.PacketsReceived,
			UnknownPortErrors:        s.UDP.UnknownPortErrors,
			ReceiveBufferErrors:      s.UDP.ReceiveBufferErrors,
			MalformedPacketsReceived: s.UDP.MalformedPacketsReceived,
			PacketsSent:              s.UDP.PacketsSent,
		},
	}, nil
}

func (ni *netstackImpl) GetStats(nicid uint32) (stats nsfidl.NetInterfaceStats, err error) {
	// Pure reading of statistics. No critical section. No lock is needed.
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return nsfidl.NetInterfaceStats{}, fmt.Errorf("no such interface id: %d", nicid)
	}

	return ifState.statsEP.Stats, nil
}

func (ni *netstackImpl) SetInterfaceStatus(nicid uint32, enabled bool) (err error) {
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return fmt.Errorf("no such interface id: %d", nicid)
	}

	if enabled {
		ifState.eth.Up()
	} else {
		ifState.eth.Down()
	}

	return nil
}

func (ni *netstackImpl) SetDhcpClientStatus(nicid uint32, enabled bool) (result nsfidl.NetErr, err error) {
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]
	if !ok {
		return nsfidl.NetErr{nsfidl.StatusUnknownInterface, "unknown interface"}, nil
	}
	ifState.setDHCPStatus(enabled)
	return nsfidl.NetErr{nsfidl.StatusOk, ""}, nil
}

func (ni *netstackImpl) onInterfacesChanged(interfaces []nsfidl.NetInterface) {
	if bindings.Proxy(ni.listener).IsValid() {
		ni.listener.OnInterfacesChanged(interfaces)
	}
}

var netstackService *nsfidl.NetstackService

// AddNetstackService registers the NetstackService with the application context,
// allowing it to respond to FIDL queries.
func AddNetstackService(ctx *context.Context) error {
	if netstackService != nil {
		return fmt.Errorf("AddNetworkService must be called only once")
	}
	netstackService = &nsfidl.NetstackService{}
	ctx.OutgoingService.AddService(nsfidl.NetstackName, func(c zx.Channel) error {
		k, err := netstackService.Add(&netstackImpl{}, c, nil)
		if err != nil {
			return err
		}
		// Send a synthetic InterfacesChanged event to each client when they join
		// Prevents clients from having to race GetInterfaces / InterfacesChanged.
		if p, ok := netstackService.EventProxyFor(k); ok {
			p.OnInterfacesChanged(getInterfaces())
		}
		return nil
	})

	ctx.OutgoingService.AddService(net.ConnectivityName, func(c zx.Channel) error {
		_, err := connectivity.Service.Add(struct{}{}, c, nil)
		return err
	})

	return nil
}

func OnInterfacesChanged() {
	if netstackService != nil {
		interfaces := getInterfaces()
		connectivity.InferAndNotify(interfaces)
		for key, client := range netstackService.Bindings {
			if p, ok := netstackService.EventProxyFor(key); ok {
				p.OnInterfacesChanged(interfaces)
			}
			// TODO(stijlist): port mDNS to use FIDL2 events instead of NotificationListener
			client.Stub.(*nsfidl.NetstackStub).Impl.(*netstackImpl).onInterfacesChanged(interfaces)
		}
	}
}
