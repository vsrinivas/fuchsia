// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fidl/fuchsia/net/stack"
	"fmt"
	"net"
	"sort"
	"syscall/zx"
	"syscall/zx/zxwait"

	"syslog"

	"netstack/fidlconv"
	"netstack/link"
	"netstack/routes"

	"fidl/fuchsia/hardware/ethernet"
	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/dhcp"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

const zeroIpAddr = header.IPv4Any

type netstackImpl struct {
	ns                *Netstack
	dhcpClientService dhcp.ClientService
}

// interfaces2ListToInterfacesList converts a NetInterface2 list into a
// NetInterface one.
func interfaces2ListToInterfacesList(ifs2 []netstack.NetInterface2) []netstack.NetInterface {
	ifs := make([]netstack.NetInterface, 0, len(ifs2))
	for _, e2 := range ifs2 {
		ifs = append(ifs, netstack.NetInterface{
			Id:        e2.Id,
			Flags:     e2.Flags,
			Features:  e2.Features,
			Name:      e2.Name,
			Addr:      e2.Addr,
			Netmask:   e2.Netmask,
			Broadaddr: e2.Broadaddr,
			Hwaddr:    e2.Hwaddr,
			Ipv6addrs: e2.Ipv6addrs,
		})
	}
	return ifs
}

func (ns *Netstack) getNetInterfaces2Locked() []netstack.NetInterface2 {
	ifStates := ns.mu.ifStates
	interfaces := make([]netstack.NetInterface2, 0, len(ifStates))
	for _, ifs := range ifStates {
		ifs.mu.Lock()
		netInterface, err := ifs.toNetInterface2Locked()
		ifs.mu.Unlock()
		if err != nil {
			syslog.Warnf("failed to call ifs.toNetInterfaceLocked: %v", err)
		}
		interfaces = append(interfaces, netInterface)
	}
	return interfaces
}

func (ifs *ifState) toNetInterface2Locked() (netstack.NetInterface2, error) {
	addrWithPrefix, err := ifs.ns.mu.stack.GetMainNICAddress(ifs.nicid, ipv4.ProtocolNumber)
	// Upstream reuses ErrNoLinkAddress to indicate no address can be found for the requested NIC and
	// network protocol.
	if err == tcpip.ErrUnknownNICID {
		panic(fmt.Sprintf("stack.GetMainNICAddress(%d, ...): %s", ifs.nicid, err))
	} else if err != nil {
		return netstack.NetInterface2{}, fmt.Errorf("stack.GetMainNICAddress(_): %s", err)
	}
	if addrWithPrefix == (tcpip.AddressWithPrefix{}) {
		addrWithPrefix = tcpip.AddressWithPrefix{Address: zeroIpAddr, PrefixLen: 0}
	}

	mask := net.CIDRMask(addrWithPrefix.PrefixLen, len(addrWithPrefix.Address)*8)
	broadaddr := []byte(addrWithPrefix.Address)
	for i := range broadaddr {
		broadaddr[i] |= ^mask[i]
	}

	nicInfo, ok := ifs.ns.mu.stack.NICInfo()[ifs.nicid]
	if !ok {
		panic(fmt.Sprintf("NIC [%d] not found in %+v", ifs.nicid, nicInfo))
	}
	ipv6addrs := make([]fidlnet.Subnet, 0, len(nicInfo.ProtocolAddresses))

	for _, address := range nicInfo.ProtocolAddresses {
		if address.Protocol == ipv6.ProtocolNumber {
			ipv6addrs = append(ipv6addrs, fidlnet.Subnet{
				Addr:      fidlconv.ToNetIpAddress(address.AddressWithPrefix.Address),
				PrefixLen: uint8(address.AddressWithPrefix.PrefixLen),
			})
		}
	}

	var flags uint32
	if ifs.mu.state == link.StateStarted {
		flags |= netstack.NetInterfaceFlagUp
	}
	if ifs.mu.dhcp.enabled {
		flags |= netstack.NetInterfaceFlagDhcp
	}

	return netstack.NetInterface2{
		Id:        uint32(ifs.nicid),
		Flags:     flags,
		Features:  ifs.features,
		Metric:    uint32(ifs.mu.metric),
		Name:      ifs.ns.nameLocked(ifs.nicid),
		Addr:      fidlconv.ToNetIpAddress(addrWithPrefix.Address),
		Netmask:   fidlconv.ToNetIpAddress(tcpip.Address(mask)),
		Broadaddr: fidlconv.ToNetIpAddress(tcpip.Address(broadaddr)),
		Hwaddr:    []uint8(ifs.endpoint.LinkAddress()[:]),
		Ipv6addrs: ipv6addrs,
	}, nil
}

func (ns *Netstack) getInterfaces2Locked() []netstack.NetInterface2 {
	out := ns.getNetInterfaces2Locked()

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

// GetInterfaces2 returns a list of interfaces.
// TODO(NET-2078): Move this to GetInterfaces once Chromium stops using
// netstack.fidl.
func (ni *netstackImpl) GetInterfaces2() ([]netstack.NetInterface2, error) {
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	return ni.ns.getInterfaces2Locked(), nil
}

// GetInterfaces is a deprecated version that returns a list of interfaces in a
// format that Chromium supports. The new version is GetInterfaces2 which
// eventually will be renamed once Chromium is not using netstack.fidl anymore
// and this deprecated version can be removed.
func (ni *netstackImpl) GetInterfaces() ([]netstack.NetInterface, error) {
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	// Get the new interface list and convert to the old one.
	return interfaces2ListToInterfacesList(ni.ns.getInterfaces2Locked()), nil
}

// TODO(NET-2078): Move this to GetRouteTable once Chromium stops using
// netstack.fidl.
func (ni *netstackImpl) GetRouteTable2() ([]netstack.RouteTableEntry2, error) {
	return nsToRouteTable2(ni.ns.GetExtendedRouteTable()), nil
}

// GetRouteTable is a deprecated version that returns the route table in a
// format that Chromium supports. The new version is GetRouteTable2 which will
// eventually be renamed once Chromium is not using netstack.fidl anymore and
// this deprecated version can be removed.
func (ni *netstackImpl) GetRouteTable() ([]netstack.RouteTableEntry, error) {
	rt2 := nsToRouteTable2(ni.ns.GetExtendedRouteTable())
	rt := make([]netstack.RouteTableEntry, 0, len(rt2))
	for _, r2 := range rt2 {
		var gateway fidlnet.IpAddress
		if r2.Gateway != nil {
			gateway = *r2.Gateway
		} else {
			gateway = fidlconv.ToNetIpAddress(zeroIpAddr)
		}
		rt = append(rt, netstack.RouteTableEntry{
			Destination: r2.Destination,
			Netmask:     r2.Netmask,
			Gateway:     gateway,
			Nicid:       r2.Nicid,
		})
	}
	return rt, nil
}

func nsToRouteTable2(table []routes.ExtendedRoute) []netstack.RouteTableEntry2 {
	out := make([]netstack.RouteTableEntry2, 0, len(table))
	for _, e := range table {
		var gatewayPtr *fidlnet.IpAddress
		if len(e.Route.Gateway) != 0 {
			gateway := fidlconv.ToNetIpAddress(e.Route.Gateway)
			gatewayPtr = &gateway
		}
		out = append(out, netstack.RouteTableEntry2{
			Destination: fidlconv.ToNetIpAddress(e.Route.Destination.ID()),
			Netmask:     fidlconv.ToNetIpAddress(tcpip.Address(e.Route.Destination.Mask())),
			Gateway:     gatewayPtr,
			Nicid:       uint32(e.Route.NIC),
			Metric:      uint32(e.Metric),
		})
	}
	return out
}

func routeToNs(r netstack.RouteTableEntry2) tcpip.Route {
	prefixLen, _ := net.IPMask(fidlconv.ToTCPIPAddress(r.Netmask)).Size()
	route := tcpip.Route{
		Destination: fidlconv.ToTCPIPSubnet(fidlnet.Subnet{
			Addr:      r.Destination,
			PrefixLen: uint8(prefixLen),
		}),
		NIC: tcpip.NICID(r.Nicid),
	}
	if g := r.Gateway; g != nil {
		route.Gateway = fidlconv.ToTCPIPAddress(*g)
	}
	return route
}

type routeTableTransactionImpl struct {
	ni *netstackImpl
}

func (i *routeTableTransactionImpl) AddRoute(r netstack.RouteTableEntry2) (int32, error) {
	err := i.ni.ns.AddRoute(routeToNs(r), routes.Metric(r.Metric), false /* not dynamic */)
	if err != nil {
		return int32(zx.ErrInvalidArgs), err
	}
	return int32(zx.ErrOk), nil
}

func (i *routeTableTransactionImpl) DelRoute(r netstack.RouteTableEntry2) (int32, error) {
	err := i.ni.ns.DelRoute(routeToNs(r))
	if err != nil {
		return int32(zx.ErrInvalidArgs), err
	}
	return int32(zx.ErrOk), nil
}

func (ni *netstackImpl) StartRouteTableTransaction(req netstack.RouteTableTransactionInterfaceRequest) (int32, error) {
	{
		ni.ns.mu.Lock()
		defer ni.ns.mu.Unlock()

		if ni.ns.mu.transactionRequest != nil {
			oldChannel := ni.ns.mu.transactionRequest.Channel
			observed, _ := zxwait.Wait(zx.Handle(oldChannel), 0, 0)
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
	transaction := routeTableTransactionImpl{ni: ni}
	// We don't use the error handler to free the channel because it's
	// possible that the peer closes the channel before our service has
	// finished processing.
	c := req.Channel
	_, err := routeTableService.Add(&transaction, c, nil)
	if err != nil {
		return int32(zx.ErrShouldWait), err
	}
	return int32(zx.ErrOk), err
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(nicid uint32, address fidlnet.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
	protocolAddr := toProtocolAddr(stack.InterfaceAddress{
		IpAddress: address,
		PrefixLen: prefixLen,
	})
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		return netstack.NetErr{Status: netstack.StatusParseError, Message: "prefix length exceeds address length"}, nil
	}

	found, err := ni.ns.addInterfaceAddress(tcpip.NICID(nicid), protocolAddr)
	if err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, nil
	}
	if !found {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface}, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk}, nil
}

func (ni *netstackImpl) RemoveInterfaceAddress(nicid uint32, address fidlnet.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
	protocolAddr := toProtocolAddr(stack.InterfaceAddress{
		IpAddress: address,
		PrefixLen: prefixLen,
	})
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		return netstack.NetErr{Status: netstack.StatusParseError, Message: "prefix length exceeds address length"}, nil
	}

	found, err := ni.ns.removeInterfaceAddress(tcpip.NICID(nicid), protocolAddr)
	if err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, nil
	}
	if !found {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface}, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk}, nil
}

// SetInterfaceMetric changes the metric for an interface and updates all
// routes tracking that interface metric. This takes the lock.
func (ni *netstackImpl) SetInterfaceMetric(nicid uint32, metric uint32) (result netstack.NetErr, err error) {
	syslog.Infof("update interface metric for NIC %d to metric=%d", nicid, metric)

	nic := tcpip.NICID(nicid)
	m := routes.Metric(metric)

	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()

	ifState, ok := ni.ns.mu.ifStates[nic]
	if !ok {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface}, nil
	}
	ifState.updateMetric(m)

	ni.ns.mu.routeTable.UpdateMetricByInterface(nic, m)
	ni.ns.mu.stack.SetRouteTable(ni.ns.mu.routeTable.GetNetstackTable())
	return netstack.NetErr{Status: netstack.StatusOk}, nil
}

func (ni *netstackImpl) BridgeInterfaces(nicids []uint32) (netstack.NetErr, uint32, error) {
	nics := make([]tcpip.NICID, len(nicids))
	for i, n := range nicids {
		nics[i] = tcpip.NICID(n)
	}
	ifs, err := ni.ns.Bridge(nics)
	if err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, 0, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk}, uint32(ifs.nicid), nil
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

func (ni *netstackImpl) GetDhcpClient(id uint32, request dhcp.ClientInterfaceRequest) (netstack.NetstackGetDhcpClientResult, error) {
	var result netstack.NetstackGetDhcpClientResult
	nicid := tcpip.NICID(id)
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	if _, ok := ni.ns.mu.ifStates[nicid]; !ok {
		result.SetErr(int32(zx.ErrNotFound))
		return result, nil
	}
	s := &dhcp.ClientStub{Impl: &clientImpl{ns: ni.ns, nicid: nicid}}
	if _, err := ni.dhcpClientService.BindingSet.Add(s, request.Channel, nil); err != nil {
		result.SetErr(int32(zx.ErrInternal))
		return result, nil
	}
	result.SetResponse(netstack.NetstackGetDhcpClientResponse{})
	return result, nil
}

func (ns *netstackImpl) AddEthernetDevice(topological_path string, interfaceConfig netstack.InterfaceConfig, device ethernet.DeviceInterface) (uint32, error) {
	ifs, err := ns.ns.addEth(topological_path, interfaceConfig, &device)
	if err != nil {
		return 0, err
	}
	return uint32(ifs.nicid), err
}

type dnsImpl struct {
	ns *Netstack
}

func (dns *dnsImpl) SetNameServers(servers []fidlnet.IpAddress) error {
	ss := make([]tcpip.Address, len(servers))

	for i, s := range servers {
		ss[i] = fidlconv.ToTCPIPAddress(s)
	}

	syslog.Infof("setting default name servers: %s", ss)
	dns.ns.dnsClient.SetDefaultServers(ss)
	return nil
}
