// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"sort"

	"syslog/logger"

	"netstack/fidlconv"
	"netstack/link"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
)

type stackImpl struct {
	ns *Netstack
}

func getInterfaceInfo(ifs *ifState) *stack.InterfaceInfo {
	ifs.mu.Lock()
	defer ifs.mu.Unlock()
	// Long-hand for: broadaddr = ifs.mu.nic.Addr | ^ifs.mu.nic.Netmask
	broadaddr := []byte(ifs.mu.nic.Addr)
	if len(ifs.mu.nic.Netmask) != len(ifs.mu.nic.Addr) {
		logger.Errorf("mismatched netmask and address length for nic: %+v", ifs.mu.nic)
		return nil
	}

	for i := range broadaddr {
		broadaddr[i] |= ^ifs.mu.nic.Netmask[i]
	}

	// TODO(tkilbourn): distinguish between enabled and link up
	administrativeStatus := stack.AdministrativeStatusDisabled
	physicalStatus := stack.PhysicalStatusDown
	if ifs.mu.state == link.StateStarted {
		administrativeStatus = stack.AdministrativeStatusEnabled
		physicalStatus = stack.PhysicalStatusUp
	}

	// TODO(tkilbourn): implement interface addresses
	addrs := make([]stack.InterfaceAddress, 0, 1)
	if len(ifs.mu.nic.Addr) > 0 {
		addrs = append(addrs, stack.InterfaceAddress{
			IpAddress: fidlconv.ToNetIpAddress(ifs.mu.nic.Addr),
			PrefixLen: fidlconv.GetPrefixLen(ifs.mu.nic.Netmask),
		})
	}

	var mac *ethernet.MacAddress
	var path string
	if eth := ifs.eth; eth != nil {
		mac = &ethernet.MacAddress{}
		copy(mac.Octets[:], ifs.endpoint.LinkAddress())
		path = eth.Path()
	}

	return &stack.InterfaceInfo{
		Id: uint64(ifs.mu.nic.ID),
		Properties: stack.InterfaceProperties{
			Path:                 path,
			Mac:                  mac,
			Mtu:                  uint32(ifs.endpoint.MTU()),
			AdministrativeStatus: administrativeStatus,
			PhysicalStatus:       physicalStatus,
			Features:             ifs.mu.nic.Features,
			Addresses:            addrs,
		},
	}
}

func (ns *Netstack) getNetInterfaces() []stack.InterfaceInfo {
	ns.mu.Lock()
	out := make([]stack.InterfaceInfo, 0, len(ns.mu.ifStates))
	for _, ifs := range ns.mu.ifStates {
		out = append(out, *getInterfaceInfo(ifs))
	}
	ns.mu.Unlock()

	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ns *Netstack) addInterface(topologicalPath string, device ethernet.DeviceInterface) (*stack.Error, uint64) {
	var interfaceConfig netstack.InterfaceConfig
	ifs, err := ns.addEth(topologicalPath, interfaceConfig, &device)
	if err != nil {
		return &stack.Error{Type: stack.ErrorTypeInternal}, 0
	}
	return nil, uint64(ifs.mu.nic.ID)
}

func (ns *Netstack) delInterface(id uint64) *stack.Error {
	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if ok {
		if err := ifs.eth.Close(); err != nil {
			logger.Errorf("ifs.eth.Close() failed (NIC: %d): %v", id, err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
		return nil
	}
	return &stack.Error{Type: stack.ErrorTypeNotFound}
}

func (ns *Netstack) getInterface(id uint64) (*stack.InterfaceInfo, *stack.Error) {
	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if ok {
		return getInterfaceInfo(ifs), nil
	}
	return nil, &stack.Error{Type: stack.ErrorTypeNotFound}
}

func (ns *Netstack) setInterfaceState(id uint64, enabled bool) *stack.Error {
	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if !ok {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	if enabled {
		if err := ifs.eth.Up(); err != nil {
			logger.Errorf("ifs.eth.Up() failed (NIC %d): %v", id, err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
	} else {
		if err := ifs.eth.Down(); err != nil {
			logger.Errorf("ifs.eth.Down() failed (NIC %d): %v", id, err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
	}
	return nil
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) *stack.Error {
	nicid := tcpip.NICID(id)

	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[nicid]
	ns.mu.Unlock()

	if !ok {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	ifs.mu.Lock()
	addr := ifs.mu.nic.Addr
	ifs.mu.Unlock()

	var protocol tcpip.NetworkProtocolNumber
	switch ifAddr.IpAddress.Which() {
	case net.IpAddressIpv4:
		if addr != header.IPv4Any {
			return &stack.Error{Type: stack.ErrorTypeAlreadyExists}
		}
		protocol = ipv4.ProtocolNumber
	case net.IpAddressIpv6:
		// TODO(tkilbourn): support IPv6 addresses (NET-1181)
		return &stack.Error{Type: stack.ErrorTypeNotSupported}
	}

	if err := ns.setInterfaceAddress(nicid, protocol, fidlconv.ToTCPIPAddress(ifAddr.IpAddress), ifAddr.PrefixLen); err != nil {
		logger.Errorf("(*Netstack).setInterfaceAddress(...) failed (NIC %d): %v", nicid, err)
		return &stack.Error{Type: stack.ErrorTypeBadState}
	}
	return nil
}

func (ns *Netstack) getForwardingTable() []stack.ForwardingEntry {
	ns.mu.Lock()
	table := ns.mu.stack.GetRouteTable()
	ns.mu.Unlock()

	entries := make([]stack.ForwardingEntry, 0)
	for _, route := range table {
		entries = append(entries, fidlconv.TcpipRouteToForwardingEntry(route))
	}
	return entries
}

// equalSubnetAndRoute returns true if and only if the route matches
// the subnet.  Only the the ip and subnet are compared and must be
// exact.
func equalSubnetAndRoute(subnet net.Subnet, tcpipRoute tcpip.Route) bool {
	return fidlconv.ToTCPIPAddress(subnet.Addr) == tcpipRoute.Destination &&
		subnet.PrefixLen == fidlconv.GetPrefixLen(tcpipRoute.Mask)
}

// validateSubnet returns true if the prefix length is valid and no
// address bits are set beyond the prefix length.
func validateSubnet(subnet net.Subnet) bool {
	var ipBytes []uint8
	switch subnet.Addr.Which() {
	case net.IpAddressIpv4:
		ipBytes = subnet.Addr.Ipv4.Addr[:]
	case net.IpAddressIpv6:
		ipBytes = subnet.Addr.Ipv6.Addr[:]
	}
	if int(subnet.PrefixLen) > len(ipBytes)*8 {
		return false
	}
	prefixBytes := subnet.PrefixLen / 8
	ipBytes = ipBytes[prefixBytes:]
	if prefixBits := subnet.PrefixLen - (prefixBytes * 8); prefixBits > 0 {
		// prefixBits is only greater than zero when ipBytes is non-empty.
		mask := uint8((1 << (8 - prefixBits)) - 1)
		ipBytes[0] &= mask
	}
	for _, byte := range ipBytes {
		if byte != 0 {
			return false
		}
	}
	return true
}

func (ns *Netstack) addForwardingEntry(entry stack.ForwardingEntry) *stack.Error {
	if !validateSubnet(entry.Subnet) {
		return &stack.Error{Type: stack.ErrorTypeInvalidArgs}
	}
	ns.mu.Lock()
	defer ns.mu.Unlock()
	table := ns.mu.stack.GetRouteTable()

	for _, route := range table {
		if equalSubnetAndRoute(entry.Subnet, route) {
			return &stack.Error{Type: stack.ErrorTypeAlreadyExists}
		}
	}
	ns.mu.stack.SetRouteTable(append(table, fidlconv.ForwardingEntryToTcpipRoute(entry)))
	return nil
}

func (ns *Netstack) delForwardingEntry(subnet net.Subnet) *stack.Error {
	if !validateSubnet(subnet) {
		return &stack.Error{Type: stack.ErrorTypeInvalidArgs}
	}
	ns.mu.Lock()
	defer ns.mu.Unlock()
	table := ns.mu.stack.GetRouteTable()

	for i, route := range table {
		if equalSubnetAndRoute(subnet, route) {
			table[i] = table[len(table)-1]
			table = table[:len(table)-1]
			ns.mu.stack.SetRouteTable(table)
			return nil
		}
	}
	return &stack.Error{Type: stack.ErrorTypeNotFound}
}

func (ni *stackImpl) AddEthernetInterface(topologicalPath string, device ethernet.DeviceInterface) (*stack.Error, uint64, error) {
	err, id := ni.ns.addInterface(topologicalPath, device)
	return err, id, nil
}

func (ni *stackImpl) DelEthernetInterface(id uint64) (*stack.Error, error) {
	return ni.ns.delInterface(id), nil
}

func (ni *stackImpl) ListInterfaces() ([]stack.InterfaceInfo, error) {
	return ni.ns.getNetInterfaces(), nil
}

func (ni *stackImpl) GetInterfaceInfo(id uint64) (*stack.InterfaceInfo, *stack.Error, error) {
	info, err := ni.ns.getInterface(id)
	return info, err, nil
}

func (ni *stackImpl) EnableInterface(id uint64) (*stack.Error, error) {
	return ni.ns.setInterfaceState(id, true), nil
}

func (ni *stackImpl) DisableInterface(id uint64) (*stack.Error, error) {
	return ni.ns.setInterfaceState(id, false), nil
}

func (ni *stackImpl) AddInterfaceAddress(id uint64, addr stack.InterfaceAddress) (*stack.Error, error) {
	return ni.ns.addInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) DelInterfaceAddress(id uint64, addr net.IpAddress) (*stack.Error, error) {
	panic("not implemented")
}

func (ni *stackImpl) GetForwardingTable() ([]stack.ForwardingEntry, error) {
	return ni.ns.getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(entry stack.ForwardingEntry) (*stack.Error, error) {
	return ni.ns.addForwardingEntry(entry), nil
}

func (ni *stackImpl) DelForwardingEntry(subnet net.Subnet) (*stack.Error, error) {
	return ni.ns.delForwardingEntry(subnet), nil
}
