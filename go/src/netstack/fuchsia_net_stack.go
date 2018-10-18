// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"sort"

	"netstack/fidlconv"
	"netstack/link/eth"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"
	"fidl/zircon/ethernet"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
)

type stackImpl struct {
	ns *Netstack
}

func getInterfaceInfo(nicid tcpip.NICID, ifs *ifState) *stack.InterfaceInfo {
	// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
	broadaddr := []byte(ifs.nic.Addr)
	if len(ifs.nic.Netmask) != len(ifs.nic.Addr) {
		log.Printf("warning: mismatched netmask and address length for nic: %+v", ifs.nic)
		return nil
	}

	for i := range broadaddr {
		broadaddr[i] |= ^ifs.nic.Netmask[i]
	}

	// TODO(tkilbourn): distinguish between enabled and link up
	var status uint32
	if ifs.state == eth.StateStarted {
		status |= stack.InterfaceStatusEnabled
	}

	features := []stack.InterfaceFeature{}
	if ifs.nic.Features&ethernet.InfoFeatureWlan != 0 {
		features = append(features, stack.InterfaceFeatureWlan)
	}
	if ifs.nic.Features&ethernet.InfoFeatureSynth != 0 {
		features = append(features, stack.InterfaceFeatureSynthetic)
	}
	if ifs.nic.Features&ethernet.InfoFeatureLoopback != 0 {
		features = append(features, stack.InterfaceFeatureLoopback)
	}

	// TODO(tkilbourn): implement interface addresses
	addrs := []stack.InterfaceAddress{}
	if len(ifs.nic.Addr) > 0 {
		addrs = append(addrs, stack.InterfaceAddress{
			IpAddress: fidlconv.ToNetIpAddress(ifs.nic.Addr),
			PrefixLen: fidlconv.GetPrefixLen(ifs.nic.Netmask),
		})
	}

	var mac net.MacAddress
	var path string
	if eth := ifs.eth; eth != nil {
		mac.Addr = eth.Info.Mac.Octets
		path = eth.Path
	}

	return &stack.InterfaceInfo{
		Id:        uint64(nicid),
		Path:      path,
		Mac:       &mac,
		Mtu:       uint32(ifs.statsEP.MTU()),
		Status:    status,
		Features:  features,
		Addresses: addrs,
	}

}

func (ns *Netstack) getNetInterfaces() []stack.InterfaceInfo {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	out := make([]stack.InterfaceInfo, 0, len(ns.ifStates))
	for nicid, ifs := range ns.ifStates {
		out = append(out, *getInterfaceInfo(nicid, ifs))
	}
	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ns *Netstack) getInterface(id uint64) (*stack.InterfaceInfo, *stack.Error) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		if uint64(nicid) == id {
			return getInterfaceInfo(nicid, ifs), nil
		}
	}
	return nil, &stack.Error{Type: stack.ErrorTypeNotFound}
}

func (ns *Netstack) setInterfaceState(id uint64, enabled bool) *stack.Error {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	var ifs *ifState
	for i, ifState := range ns.ifStates {
		if uint64(i) == id {
			ifs = ifState
			break
		}
	}
	if ifs == nil {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	if enabled {
		if err := ifs.eth.Up(); err != nil {
			log.Printf("ifs.eth.Up() failed: %v", err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
	} else {
		if err := ifs.eth.Down(); err != nil {
			log.Printf("ifs.eth.Down() failed: %v", err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
	}
	return nil
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) *stack.Error {
	// The ns mutex is held in the setInterfaceAddress call below so release it
	// after we find the right ifState.
	ns.mu.Lock()
	var ifs *ifState
	for i, ifState := range ns.ifStates {
		if uint64(i) == id {
			ifs = ifState
			break
		}
	}
	ns.mu.Unlock()

	if ifs == nil {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	var protocol tcpip.NetworkProtocolNumber
	switch ifAddr.IpAddress.Which() {
	case net.IpAddressIpv4:
		if len(ifs.nic.Addr) > 0 {
			return &stack.Error{Type: stack.ErrorTypeAlreadyExists}
		}
		protocol = ipv4.ProtocolNumber
	case net.IpAddressIpv6:
		// TODO(tkilbourn): support IPv6 addresses (NET-1181)
		return &stack.Error{Type: stack.ErrorTypeNotSupported}
	}

	nic := ifs.nic.ID
	addr := fidlconv.ToTCPIPAddress(ifAddr.IpAddress)
	if err := ns.setInterfaceAddress(nic, protocol, addr, ifAddr.PrefixLen); err != nil {
		log.Printf("(*Netstack).setInterfaceAddress(...) failed: %v", err)
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

func equalNetAddress(a netstack.NetAddress, b netstack.NetAddress) bool {
	if a.Family != b.Family {
		return false
	}
	switch a.Family {
	case netstack.NetAddressFamilyIpv4:
		return a.Ipv4.Addr == b.Ipv4.Addr
	case netstack.NetAddressFamilyIpv6:
		return a.Ipv6.Addr == b.Ipv6.Addr
	default:
		return false
	}
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
