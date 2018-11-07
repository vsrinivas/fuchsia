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
	"syscall/zx/zxwait"

	"netstack/fidlconv"
	"netstack/link/eth"

	"fidl/fuchsia/netstack"
	"fidl/zircon/ethernet"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct {
	ns *Netstack
}

func toSubnets(addrs []tcpip.Address) []netstack.Subnet {
	out := make([]netstack.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = netstack.Subnet{Addr: fidlconv.ToNetAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func getInterfaces(ns *Netstack) (out []netstack.NetInterface) {
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
		if ifs.dhcpState.configured {
			flags |= netstack.NetInterfaceFlagDhcp
		}

		var mac []uint8
		if eth := ifs.eth; eth != nil {
			mac = eth.Info.Mac.Octets[:]
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

func (ni *netstackImpl) GetAddress(name string, port uint16) ([]netstack.SocketAddress, netstack.NetErr, error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// fdio's getaddrinfo into here.
	addrs, err := ni.ns.dnsClient.LookupIP(name)
	if err != nil {
		return nil, netstack.NetErr{Status: netstack.StatusDnsError, Message: err.Error()}, nil
	}
	out := make([]netstack.SocketAddress, 0, len(addrs))
	for _, addr := range addrs {
		out = append(out, netstack.SocketAddress{
			Addr: fidlconv.ToNetAddress(addr),
			Port: port,
		})
	}
	return out, netstack.NetErr{Status: netstack.StatusOk}, nil
}

func (ni *netstackImpl) GetInterfaces() (out []netstack.NetInterface, err error) {
	return getInterfaces(ni.ns), nil
}

func (ni *netstackImpl) GetRouteTable() (out []netstack.RouteTableEntry, err error) {
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	table := ni.ns.mu.stack.GetRouteTable()
	return nsToRouteTable(table)
}

func nsToRouteTable(table []tcpip.Route) (out []netstack.RouteTableEntry, err error) {
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
			mask = tcpip.AddressMask(strings.Repeat("\x00", l))
		}
		if len(gateway) == 0 {
			gateway = tcpip.Address(strings.Repeat("\x00", l))
		}

		out = append(out, netstack.RouteTableEntry{
			Destination: fidlconv.ToNetAddress(dest),
			Netmask:     fidlconv.ToNetAddress(tcpip.Address(mask)),
			Gateway:     fidlconv.ToNetAddress(gateway),
			Nicid:       uint32(route.NIC),
		})
	}
	return out, nil
}

func routeTableToNs(rt []netstack.RouteTableEntry) []tcpip.Route {
	routes := make([]tcpip.Route, 0, len(rt))
	for _, r := range rt {
		routes = append(routes, tcpip.Route{
			Destination: fidlconv.NetAddressToTCPIPAddress(r.Destination),
			Mask:        tcpip.AddressMask(fidlconv.NetAddressToTCPIPAddress(r.Netmask)),
			Gateway:     fidlconv.NetAddressToTCPIPAddress(r.Gateway),
			NIC:         tcpip.NICID(r.Nicid),
		})
	}

	return routes
}

type routeTableTransactionImpl struct {
	ni              *netstackImpl
	routeTableCache []tcpip.Route
}

func (i *routeTableTransactionImpl) GetRouteTable() (out []netstack.RouteTableEntry, err error) {
	return nsToRouteTable(i.routeTableCache)
}

func (i *routeTableTransactionImpl) SetRouteTable(rt []netstack.RouteTableEntry) error {
	routes := routeTableToNs(rt)
	i.routeTableCache = routes
	return nil
}

func (i *routeTableTransactionImpl) Commit() (int32, error) {
	i.ni.ns.mu.Lock()
	defer i.ni.ns.mu.Unlock()
	i.ni.ns.mu.stack.SetRouteTable(i.routeTableCache)
	return int32(zx.ErrOk), nil
}

func (ni *netstackImpl) StartRouteTableTransaction(req netstack.RouteTableTransactionInterfaceRequest) (int32, error) {
	{
		ni.ns.mu.Lock()
		defer ni.ns.mu.Unlock()

		if ni.ns.mu.transactionRequest != nil {
			oldChannel := ni.ns.mu.transactionRequest.ToChannel()
			observed, _ := zxwait.Wait(*oldChannel.Handle(), 0, 0)
			// If the channel is neither readable nor writable, there is no
			// data left to be processed (not readable) and we can't return
			// any more results (not writable).  It's not enough to only
			// look at peerclosed because the peer can close the channel
			// while it still has data in its buffers.
			if observed&(zx.SignalChannelReadable|zx.SignalChannelWritable) == 0 {
				ni.ns.mu.transactionRequest = nil
			}
		}
		if ni.ns.mu.transactionRequest != nil {
			return int32(zx.ErrShouldWait), nil
		}
		ni.ns.mu.transactionRequest = &req
	}
	var routeTableService netstack.RouteTableTransactionService
	transaction := routeTableTransactionImpl{
		ni:              ni,
		routeTableCache: ni.ns.mu.stack.GetRouteTable(),
	}
	// We don't use the error handler to free the channel because it's
	// possible that the peer closes the channel before our service has
	// finished processing.
	c := req.ToChannel()
	_, err := routeTableService.Add(&transaction, c, nil)
	if err != nil {
		return int32(zx.ErrShouldWait), err
	}
	return int32(zx.ErrOk), err
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(nicid uint32, address netstack.NetAddress, prefixLen uint8) (result netstack.NetErr, endService error) {
	log.Printf("net address %+v", address)

	nic := tcpip.NICID(nicid)
	protocol, addr, neterr := ni.ns.validateInterfaceAddress(address, prefixLen)
	if neterr.Status != netstack.StatusOk {
		return neterr, nil
	}

	if err := ni.ns.setInterfaceAddress(nic, protocol, addr, prefixLen); err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

func (ni *netstackImpl) RemoveInterfaceAddress(nicid uint32, address netstack.NetAddress, prefixLen uint8) (result netstack.NetErr, endService error) {
	nic := tcpip.NICID(nicid)
	protocol, addr, neterr := ni.ns.validateInterfaceAddress(address, prefixLen)

	if neterr.Status != netstack.StatusOk {
		return neterr, nil
	}

	if err := ni.ns.removeInterfaceAddress(nic, protocol, addr, prefixLen); err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, nil
	}

	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

func (ni *netstackImpl) BridgeInterfaces(nicids []uint32) (netstack.NetErr, error) {
	nics := make([]tcpip.NICID, len(nicids))
	for i, n := range nicids {
		nics[i] = tcpip.NICID(n)
	}
	_, err := ni.ns.Bridge(nics)
	if err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError}, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk}, nil
}

func (ni *netstackImpl) GetAggregateStats() (stats netstack.AggregateStats, err error) {
	s := ni.ns.mu.stack.Stats()
	return netstack.AggregateStats{
		UnknownProtocolReceivedPackets: s.UnknownProtocolRcvdPackets.Value(),
		MalformedReceivedPackets:       s.MalformedRcvdPackets.Value(),
		DroppedPackets:                 s.DroppedPackets.Value(),
		IpStats: netstack.IpStats{
			PacketsReceived:          s.IP.PacketsReceived.Value(),
			InvalidAddressesReceived: s.IP.InvalidAddressesReceived.Value(),
			PacketsDelivered:         s.IP.PacketsDelivered.Value(),
			PacketsSent:              s.IP.PacketsSent.Value(),
			OutgoingPacketErrors:     s.IP.OutgoingPacketErrors.Value(),
		},
		TcpStats: netstack.TcpStats{
			ActiveConnectionOpenings:  s.TCP.ActiveConnectionOpenings.Value(),
			PassiveConnectionOpenings: s.TCP.PassiveConnectionOpenings.Value(),
			FailedConnectionAttempts:  s.TCP.FailedConnectionAttempts.Value(),
			ValidSegmentsReceived:     s.TCP.ValidSegmentsReceived.Value(),
			InvalidSegmentsReceived:   s.TCP.InvalidSegmentsReceived.Value(),
			SegmentsSent:              s.TCP.SegmentsSent.Value(),
			ResetsSent:                s.TCP.ResetsSent.Value(),
		},
		UdpStats: netstack.UdpStats{
			PacketsReceived:          s.UDP.PacketsReceived.Value(),
			UnknownPortErrors:        s.UDP.UnknownPortErrors.Value(),
			ReceiveBufferErrors:      s.UDP.ReceiveBufferErrors.Value(),
			MalformedPacketsReceived: s.UDP.MalformedPacketsReceived.Value(),
			PacketsSent:              s.UDP.PacketsSent.Value(),
		},
	}, nil
}

func (ni *netstackImpl) GetStats(nicid uint32) (stats netstack.NetInterfaceStats, err error) {
	// Pure reading of statistics. No critical section. No lock is needed.
	ifState, ok := ni.ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return netstack.NetInterfaceStats{}, fmt.Errorf("no such interface id: %d", nicid)
	}

	return ifState.statsEP.Stats, nil
}

func (ni *netstackImpl) SetInterfaceStatus(nicid uint32, enabled bool) error {
	if ifState, ok := ni.ns.ifStates[tcpip.NICID(nicid)]; ok {
		if enabled {
			return ifState.eth.Up()
		}
		return ifState.eth.Down()
	}

	// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
	return fmt.Errorf("no such interface id: %d", nicid)
}

func (ni *netstackImpl) SetDhcpClientStatus(nicid uint32, enabled bool) (result netstack.NetErr, err error) {
	ifState, ok := ni.ns.ifStates[tcpip.NICID(nicid)]
	if !ok {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface, Message: "unknown interface"}, nil
	}

	ifState.setDHCPStatus(enabled)
	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

// TODO(NET-1263): Remove once clients registering with the ResolverAdmin interface
// does not crash netstack.
func (ni *netstackImpl) SetNameServers(servers []netstack.NetAddress) error {
	d := dnsImpl{ns: ni.ns}
	return d.SetNameServers(servers)
}

type dnsImpl struct {
	ns *Netstack
}

func (dns *dnsImpl) SetNameServers(servers []netstack.NetAddress) error {
	ss := make([]tcpip.Address, len(servers))

	for i, s := range servers {
		ss[i] = fidlconv.NetAddressToTCPIPAddress(s)
	}

	dns.ns.dnsClient.SetDefaultServers(ss)
	return nil
}

func (dns *dnsImpl) GetNameServers() ([]netstack.NetAddress, error) {
	servers := dns.ns.getDNSServers()
	out := make([]netstack.NetAddress, len(servers))

	for i, s := range servers {
		out[i] = fidlconv.ToNetAddress(s)
	}

	return out, nil
}

func (ns *netstackImpl) AddEthernetDevice(topological_path string, interfaceConfig netstack.InterfaceConfig, device ethernet.DeviceInterface) error {
	_, err := ns.ns.addEth(topological_path, interfaceConfig, &device)
	return err
}
