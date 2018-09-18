// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"sort"
	"strings"
	"syscall/zx"

	"app/context"
	"netstack/connectivity"
	"netstack/fidlconv"
	"netstack/link/eth"

	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct {
}

func toSubnets(addrs []tcpip.Address) []netstack.Subnet {
	out := make([]netstack.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = netstack.Subnet{Addr: fidlconv.ToNetAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func getInterfaces() (out []netstack.NetInterface) {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	for nicid, ifs := range ns.ifStates {
		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		if len(ifs.nic.Netmask) != len(ifs.nic.Addr) {
			log.Printf("warning: mismatched netmask and address length for nic: %+v\n", ifs.nic)
			continue
		}

		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}

		var flags uint32
		if ifs.state == eth.StateStarted {
			flags |= netstack.NetInterfaceFlagUp
		}
		if ifs.dhcpState.enabled {
			flags |= netstack.NetInterfaceFlagDhcp
		}

		var mac []uint8
		if eth := ifs.eth; eth != nil {
			mac = eth.Info.MAC[:]
		}

		outif := netstack.NetInterface{
			Id:        uint32(nicid),
			Flags:     flags,
			Features:  ifs.nic.Features,
			Name:      ifs.nic.Name,
			Addr:      fidlconv.ToNetAddress(ifs.nic.Addr),
			Netmask:   fidlconv.ToNetAddress(tcpip.Address(ifs.nic.Netmask)),
			Broadaddr: fidlconv.ToNetAddress(tcpip.Address(broadaddr)),
			Hwaddr:    mac,
			Ipv6addrs: toSubnets(ifs.nic.Ipv6addrs),
		}

		out = append(out, outif)
	}

	sort.Slice(out, func(i, j int) bool {
		return out[i].Id < out[j].Id
	})

	return out
}

func (ni *netstackImpl) GetPortForService(service string, protocol netstack.Protocol) (port uint16, err error) {
	switch protocol {
	case netstack.ProtocolUdp:
		port, err = serviceLookup(service, udp.ProtocolNumber)
	case netstack.ProtocolTcp:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
	default:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
		if err != nil {
			port, err = serviceLookup(service, udp.ProtocolNumber)
		}
	}
	return port, err
}

func (ni *netstackImpl) GetAddress(name string, port uint16) (out []netstack.SocketAddress, netErr netstack.NetErr, retErr error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// fdio's getaddrinfo into here.
	addrs, err := ns.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]netstack.SocketAddress, len(addrs))
		netErr = netstack.NetErr{Status: netstack.StatusOk}
		for i, addr := range addrs {
			switch len(addr) {
			case 4, 16:
				out[i].Addr = fidlconv.ToNetAddress(addr)
				out[i].Port = port
			}
		}
	} else {
		netErr = netstack.NetErr{Status: netstack.StatusDnsError, Message: err.Error()}
	}
	return out, netErr, nil
}

func (ni *netstackImpl) GetInterfaces() (out []netstack.NetInterface, err error) {
	return getInterfaces(), nil
}

func (ni *netstackImpl) GetRouteTable() (out []netstack.RouteTableEntry, err error) {
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

		out = append(out, netstack.RouteTableEntry{
			Destination: fidlconv.ToNetAddress(dest),
			Netmask:     fidlconv.ToNetAddress(mask),
			Gateway:     fidlconv.ToNetAddress(gateway),
			Nicid:       uint32(route.NIC),
		})
	}
	return out, nil
}

func (ni *netstackImpl) SetRouteTable(rt []netstack.RouteTableEntry) error {
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

func validateInterfaceAddress(nicid uint32, address netstack.NetAddress, prefixLen uint8) (nic tcpip.NICID, protocol tcpip.NetworkProtocolNumber, addr tcpip.Address, retval netstack.NetErr) {
	switch address.Family {
	case netstack.NetAddressFamilyIpv4:
		protocol = ipv4.ProtocolNumber
	case netstack.NetAddressFamilyIpv6:
		retval = netstack.NetErr{Status: netstack.StatusIpv4Only, Message: "IPv6 not yet supported"}
		return
	}

	nic = tcpip.NICID(nicid)
	addr = fidlconv.NetAddressToTCPIPAddress(address)

	if (8 * len(addr)) < int(prefixLen) {
		retval = netstack.NetErr{Status: netstack.StatusParseError, Message: "Prefix length does not match address"}
		return
	}

	retval = netstack.NetErr{Status: netstack.StatusOk, Message: ""}
	return
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(nicid uint32, address netstack.NetAddress, prefixLen uint8) (result netstack.NetErr, endService error) {
	log.Printf("net address %+v", address)

	nic, protocol, addr, neterr := validateInterfaceAddress(nicid, address, prefixLen)
	if neterr.Status != netstack.StatusOk {
		return neterr, nil
	}

	if err := ns.setInterfaceAddress(nic, protocol, addr, prefixLen); err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

func (ni *netstackImpl) RemoveInterfaceAddress(nicid uint32, address netstack.NetAddress, prefixLen uint8) (result netstack.NetErr, endService error) {
	nic, protocol, addr, neterr := validateInterfaceAddress(nicid, address, prefixLen)

	if neterr.Status != netstack.StatusOk {
		return neterr, nil
	}

	if err := ns.removeInterfaceAddress(nic, protocol, addr, prefixLen); err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, nil
	}

	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

func (ni *netstackImpl) BridgeInterfaces(nicids []uint32) (netstack.NetErr, error) {
	nics := make([]tcpip.NICID, len(nicids))
	for i, n := range nicids {
		nics[i] = tcpip.NICID(n)
	}
	err := ns.Bridge(nics)
	if err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError}, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk}, nil
}

func (ni *netstackImpl) SetFilterStatus(enabled bool) (result netstack.NetErr, err error) {
	ns.filter.Enable(enabled)
	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

func (ni *netstackImpl) GetFilterStatus() (enabled bool, err error) {
	return ns.filter.IsEnabled(), nil
}

func (ni *netstackImpl) GetAggregateStats() (stats netstack.AggregateStats, err error) {
	s := ns.stack.Stats()
	return netstack.AggregateStats{
		UnknownProtocolReceivedPackets: s.UnknownProtocolRcvdPackets,
		MalformedReceivedPackets:       s.MalformedRcvdPackets,
		DroppedPackets:                 s.DroppedPackets,
		IpStats: netstack.IpStats{
			PacketsReceived:          s.IP.PacketsReceived,
			InvalidAddressesReceived: s.IP.InvalidAddressesReceived,
			PacketsDiscarded:         s.IP.PacketsDiscarded,
			PacketsDelivered:         s.IP.PacketsDelivered,
			PacketsSent:              s.IP.PacketsSent,
			OutgoingPacketErrors:     s.IP.OutgoingPacketErrors,
		},
		TcpStats: netstack.TcpStats{
			ActiveConnectionOpenings:  s.TCP.ActiveConnectionOpenings,
			PassiveConnectionOpenings: s.TCP.PassiveConnectionOpenings,
			FailedConnectionAttempts:  s.TCP.FailedConnectionAttempts,
			ValidSegmentsReceived:     s.TCP.ValidSegmentsReceived,
			InvalidSegmentsReceived:   s.TCP.InvalidSegmentsReceived,
			SegmentsSent:              s.TCP.SegmentsSent,
			ResetsSent:                s.TCP.ResetsSent,
		},
		UdpStats: netstack.UdpStats{
			PacketsReceived:          s.UDP.PacketsReceived,
			UnknownPortErrors:        s.UDP.UnknownPortErrors,
			ReceiveBufferErrors:      s.UDP.ReceiveBufferErrors,
			MalformedPacketsReceived: s.UDP.MalformedPacketsReceived,
			PacketsSent:              s.UDP.PacketsSent,
		},
	}, nil
}

func (ni *netstackImpl) GetStats(nicid uint32) (stats netstack.NetInterfaceStats, err error) {
	// Pure reading of statistics. No critical section. No lock is needed.
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return netstack.NetInterfaceStats{}, fmt.Errorf("no such interface id: %d", nicid)
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

func (ni *netstackImpl) SetDhcpClientStatus(nicid uint32, enabled bool) (result netstack.NetErr, err error) {
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]
	if !ok {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface, Message: "unknown interface"}, nil
	}

	ifState.setDHCPStatus(enabled)
	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

// TODO(NET-1263): Remove once clients registering with the ResolverAdmin interface
// does not crash netstack.
func (ni *netstackImpl) SetNameServers(servers []netstack.NetAddress) error {
	d := dnsImpl{}
	return d.SetNameServers(servers)
}

type dnsImpl struct{}

func (*dnsImpl) SetNameServers(servers []netstack.NetAddress) error {
	ss := make([]tcpip.Address, len(servers))

	for i, s := range servers {
		ss[i] = fidlconv.NetAddressToTCPIPAddress(s)
	}

	ns.dnsClient.SetDefaultServers(ss)
	return nil
}

func (*dnsImpl) GetNameServers() ([]netstack.NetAddress, error) {
	servers := ns.getDNSServers()
	out := make([]netstack.NetAddress, len(servers))

	for i, s := range servers {
		out[i] = fidlconv.ToNetAddress(s)
	}

	return out, nil
}

var netstackService *netstack.NetstackService

// TODO(NET-1263): register resolver admin service once clients don't crash netstack
// var dnsService *netstack.ResolverAdminService

// AddNetstackService registers the NetstackService with the application context,
// allowing it to respond to FIDL queries.
func AddNetstackService(ctx *context.Context) error {
	if netstackService != nil {
		return fmt.Errorf("AddNetworkService must be called only once")
	}
	netstackService = &netstack.NetstackService{}
	ctx.OutgoingService.AddService(netstack.NetstackName, func(c zx.Channel) error {
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

	// TODO(NET-1263): register resolver admin service once clients don't crash netstack
	// when registering.
	// ctx.OutgoingService.AddService(netstack.ResolverAdminName, func(c zx.Channel) error {
	//   _, err := dnsService.Add(&dnsImpl{}, c, nil)
	//   return err
	// })

	return nil
}

func OnInterfacesChanged() {
	if netstackService != nil {
		interfaces := getInterfaces()
		connectivity.InferAndNotify(interfaces)
		for key := range netstackService.Bindings {
			if p, ok := netstackService.EventProxyFor(key); ok {
				p.OnInterfacesChanged(interfaces)
			}
		}
	}
}
