// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"log"
	"sort"
	"strings"
	"syscall/zx"
	"syscall/zx/zxwait"

	"netstack/fidlconv"
	"netstack/link"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct {
	ns *Netstack
}

func toSubnets(addrs []tcpip.Address) []net.Subnet {
	out := make([]net.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = net.Subnet{Addr: fidlconv.ToNetIpAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func (ns *Netstack) getNetInterfacesLocked() []netstack.NetInterface {
	ifStates := ns.mu.ifStates
	interfaces := make([]netstack.NetInterface, 0, len(ifStates))
	for _, ifs := range ifStates {
		ifs.mu.Lock()
		netinterface, err := ifs.toNetInterfaceLocked()
		ifs.mu.Unlock()
		if err != nil {
			log.Print(err)
		}
		interfaces = append(interfaces, netinterface)
	}
	return interfaces
}

func (ifs *ifState) toNetInterfaceLocked() (netstack.NetInterface, error) {
	// Long-hand for: broadaddr = ifs.mu.nic.Addr | ^ifs.mu.nic.Netmask
	broadaddr := []byte(ifs.mu.nic.Addr)
	if len(ifs.mu.nic.Netmask) != len(ifs.mu.nic.Addr) {
		return netstack.NetInterface{}, fmt.Errorf("address length doesn't match netmask: %+v\n", ifs.mu.nic)
	}

	for i := range broadaddr {
		broadaddr[i] |= ^ifs.mu.nic.Netmask[i]
	}

	var flags uint32
	if ifs.mu.state == link.StateStarted {
		flags |= netstack.NetInterfaceFlagUp
	}
	if ifs.mu.dhcp.enabled {
		flags |= netstack.NetInterfaceFlagDhcp
	}

	return netstack.NetInterface{
		Id:        uint32(ifs.mu.nic.ID),
		Flags:     flags,
		Features:  ifs.mu.nic.Features,
		Name:      ifs.mu.nic.Name,
		Addr:      fidlconv.ToNetIpAddress(ifs.mu.nic.Addr),
		Netmask:   fidlconv.ToNetIpAddress(tcpip.Address(ifs.mu.nic.Netmask)),
		Broadaddr: fidlconv.ToNetIpAddress(tcpip.Address(broadaddr)),
		Hwaddr:    []uint8(ifs.statsEP.LinkAddress()[:]),
		Ipv6addrs: toSubnets(ifs.mu.nic.Ipv6addrs),
	}, nil
}

func (ns *Netstack) getInterfacesLocked() []netstack.NetInterface {
	out := ns.getNetInterfacesLocked()

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
			Addr: fidlconv.ToNetIpAddress(addr),
			Port: port,
		})
	}
	return out, netstack.NetErr{Status: netstack.StatusOk}, nil
}

func (ni *netstackImpl) GetInterfaces() ([]netstack.NetInterface, error) {
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	return ni.ns.getInterfacesLocked(), nil
}

func (ni *netstackImpl) GetRouteTable() ([]netstack.RouteTableEntry, error) {
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	table := ni.ns.mu.stack.GetRouteTable()
	return nsToRouteTable(table)
}

func nsToRouteTable(table []tcpip.Route) ([]netstack.RouteTableEntry, error) {
	out := []netstack.RouteTableEntry{}
	for _, route := range table {
		// Ensure that if any of the returned addresses are "empty",
		// they still have the appropriate length.
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
			Destination: fidlconv.ToNetIpAddress(dest),
			Netmask:     fidlconv.ToNetIpAddress(tcpip.Address(mask)),
			Gateway:     fidlconv.ToNetIpAddress(gateway),
			Nicid:       uint32(route.NIC),
		})
	}
	return out, nil
}

func routeTableToNs(rt []netstack.RouteTableEntry) []tcpip.Route {
	routes := make([]tcpip.Route, 0, len(rt))
	for _, r := range rt {
		routes = append(routes, tcpip.Route{
			Destination: fidlconv.ToTCPIPAddress(r.Destination),
			Mask:        tcpip.AddressMask(fidlconv.ToTCPIPAddress(r.Netmask)),
			Gateway:     fidlconv.ToTCPIPAddress(r.Gateway),
			NIC:         tcpip.NICID(r.Nicid),
		})
	}

	return routes
}

type routeTableTransactionImpl struct {
	ni              *netstackImpl
	routeTableCache []tcpip.Route
}

func (i *routeTableTransactionImpl) GetRouteTable() ([]netstack.RouteTableEntry, error) {
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
func (ni *netstackImpl) SetInterfaceAddress(nicid uint32, address net.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
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

func (ni *netstackImpl) RemoveInterfaceAddress(nicid uint32, address net.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
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
	ni.ns.mu.Lock()
	ifState, ok := ni.ns.mu.ifStates[tcpip.NICID(nicid)]
	ni.ns.mu.Unlock()

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return netstack.NetInterfaceStats{}, fmt.Errorf("no such interface id: %d", nicid)
	}

	return ifState.statsEP.Stats, nil
}

func (ni *netstackImpl) SetInterfaceStatus(nicid uint32, enabled bool) error {
	ni.ns.mu.Lock()
	ifState, ok := ni.ns.mu.ifStates[tcpip.NICID(nicid)]
	ni.ns.mu.Unlock()

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return fmt.Errorf("no such interface id: %d", nicid)
	}

	if enabled {
		return ifState.eth.Up()
	}
	return ifState.eth.Down()
}

func (ni *netstackImpl) SetDhcpClientStatus(nicid uint32, enabled bool) (netstack.NetErr, error) {
	ni.ns.mu.Lock()
	ifState, ok := ni.ns.mu.ifStates[tcpip.NICID(nicid)]
	ni.ns.mu.Unlock()

	if !ok {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface, Message: "unknown interface"}, nil
	}

	ifState.mu.Lock()
	ifState.setDHCPStatusLocked(enabled)
	ifState.mu.Unlock()
	return netstack.NetErr{Status: netstack.StatusOk, Message: ""}, nil
}

// TODO(NET-1263): Remove once clients registering with the ResolverAdmin interface
// does not crash netstack.
func (ni *netstackImpl) SetNameServers(servers []net.IpAddress) error {
	d := dnsImpl{ns: ni.ns}
	return d.SetNameServers(servers)
}

type dnsImpl struct {
	ns *Netstack
}

func (dns *dnsImpl) SetNameServers(servers []net.IpAddress) error {
	ss := make([]tcpip.Address, len(servers))

	for i, s := range servers {
		ss[i] = fidlconv.ToTCPIPAddress(s)
	}

	dns.ns.dnsClient.SetDefaultServers(ss)
	return nil
}

func (dns *dnsImpl) GetNameServers() ([]net.IpAddress, error) {
	servers := dns.ns.getDNSServers()
	out := make([]net.IpAddress, len(servers))

	for i, s := range servers {
		out[i] = fidlconv.ToNetIpAddress(s)
	}

	return out, nil
}

func (ns *netstackImpl) AddEthernetDevice(topological_path string, interfaceConfig netstack.InterfaceConfig, device ethernet.DeviceInterface) (uint32, error) {
	ifs, err := ns.ns.addEth(topological_path, interfaceConfig, &device)
	if err != nil {
		return 0, err
	}
	return uint32(ifs.mu.nic.ID), err
}
