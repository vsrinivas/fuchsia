// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"sort"

	"app/context"
	"netstack/fidlconv"
	"netstack/link/eth"
	"syscall/zx"

	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	nsfidl "fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
)

type stackImpl struct{}

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

	mac := &netfidl.MacAddress{Addr: [6]uint8{}}
	copy(mac.Addr[:], ifs.nic.Mac[:])

	// TODO(tkilbourn): distinguish between enabled and link up
	var status uint32 = 0
	if ifs.state == eth.StateStarted {
		status |= stack.InterfaceStatusEnabled
	}

	features := []stack.InterfaceFeature{}
	if ifs.nic.Features&nsfidl.InterfaceFeatureWlan != 0 {
		features = append(features, stack.InterfaceFeatureWlan)
	}
	if ifs.nic.Features&nsfidl.InterfaceFeatureSynth != 0 {
		features = append(features, stack.InterfaceFeatureSynthetic)
	}
	if ifs.nic.Features&nsfidl.InterfaceFeatureLoopback != 0 {
		features = append(features, stack.InterfaceFeatureLoopback)
	}

	// TODO(tkilbourn): implement interface addresses
	addrs := []stack.InterfaceAddress{}
	if len(ifs.nic.Addr) > 0 {
		addrs = append(addrs, stack.InterfaceAddress{
			IpAddress: fidlconv.ToNetIpAddress(ifs.nic.Addr),
			PrefixLen: fidlconv.GetPrefixLen(tcpip.Address(ifs.nic.Netmask)),
		})
	}

	path := ""
	if ifs.eth != nil {
		path = ifs.eth.Path
	}
	return &stack.InterfaceInfo{
		Id:        uint64(nicid),
		Path:      path,
		Mac:       mac,
		Mtu:       uint32(ifs.statsEP.MTU()),
		Status:    status,
		Features:  features,
		Addresses: addrs,
	}

}

func getNetInterfaces() (out []stack.InterfaceInfo) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		out = append(out, *getInterfaceInfo(nicid, ifs))
	}
	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func getInterface(id uint64) (*stack.InterfaceInfo, *stack.Error) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		if uint64(nicid) == id {
			return getInterfaceInfo(nicid, ifs), nil
		}
	}
	return nil, &stack.Error{Type: stack.ErrorTypeNotFound}
}

func setInterfaceState(id uint64, enabled bool) *stack.Error {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	var ifs *ifState = nil
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
		ifs.eth.Up()
	} else {
		ifs.eth.Down()
	}
	return nil
}

func addInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) *stack.Error {
	// The ns mutex is held in the setInterfaceAddress call below so release it
	// after we find the right ifState.
	ns.mu.Lock()
	var ifs *ifState = nil
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
	case netfidl.IpAddressIpv4:
		if len(ifs.nic.Addr) > 0 {
			return &stack.Error{Type: stack.ErrorTypeAlreadyExists}
		}
		protocol = ipv4.ProtocolNumber
	case netfidl.IpAddressIpv6:
		// TODO(tkilbourn): support IPv6 addresses (NET-1181)
		return &stack.Error{Type: stack.ErrorTypeNotSupported}
	}

	nic := ifs.nic.ID
	addr := fidlconv.ToTCPIPAddress(ifAddr.IpAddress)
	if err := ns.setInterfaceAddress(nic, protocol, addr, ifAddr.PrefixLen); err != nil {
		return &stack.Error{Type: stack.ErrorTypeBadState}
	}
	return nil
}

func getForwardingTable() []stack.ForwardingEntry {
	ns.mu.Lock()
	table := ns.stack.GetRouteTable()
	ns.mu.Unlock()

	entries := make([]stack.ForwardingEntry, 0)
	for _, route := range table {
		dest := stack.ForwardingDestination{}
		// There are two types of destinations: link-local and next-hop.
		//   If a route has a gateway, use that as the next-hop, and ignore the NIC.
		//   Otherwise, it is considered link-local, and use the NIC.
		if route.Gateway == tcpip.Address("") {
			dest.SetDeviceId(uint64(route.NIC))
		} else {
			dest.SetNextHop(fidlconv.ToNetIpAddress(route.Gateway))
		}
		entry := stack.ForwardingEntry{
			Subnet: netfidl.Subnet{
				Addr:      fidlconv.ToNetIpAddress(route.Destination),
				PrefixLen: fidlconv.GetPrefixLen(route.Mask),
			},
			Destination: dest,
		}
		entries = append(entries, entry)
	}
	return entries
}

func (ni *stackImpl) ListInterfaces() ([]stack.InterfaceInfo, error) {
	return getNetInterfaces(), nil
}

func (ni *stackImpl) GetInterfaceInfo(id uint64) (*stack.InterfaceInfo, *stack.Error, error) {
	info, err := getInterface(id)
	return info, err, nil
}

func (ni *stackImpl) EnableInterface(id uint64) (*stack.Error, error) {
	return setInterfaceState(id, true), nil
}

func (ni *stackImpl) DisableInterface(id uint64) (*stack.Error, error) {
	return setInterfaceState(id, false), nil
}

func (ni *stackImpl) AddInterfaceAddress(id uint64, addr stack.InterfaceAddress) (*stack.Error, error) {
	return addInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) DelInterfaceAddress(id uint64, addr netfidl.IpAddress) (*stack.Error, error) {
	panic("not implemented")
}

func (ni *stackImpl) GetForwardingTable() ([]stack.ForwardingEntry, error) {
	return getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(entry stack.ForwardingEntry) (*stack.Error, error) {
	panic("not implemented")
}

func (ni *stackImpl) DelForwardingEntry(subset netfidl.Subnet) (*stack.Error, error) {
	panic("not implemented")
}

var stackService *stack.StackService

// AddStackService registers the StackService with the application context,
// allowing it to respond to FIDL queries.
func AddStackService(ctx *context.Context) error {
	if stackService != nil {
		return fmt.Errorf("AddNetworkService must be called only once")
	}
	stackService = &stack.StackService{}
	ctx.OutgoingService.AddService(stack.StackName, func(c zx.Channel) error {
		_, err := stackService.Add(&stackImpl{}, c, nil)
		return err
	})

	return nil
}
