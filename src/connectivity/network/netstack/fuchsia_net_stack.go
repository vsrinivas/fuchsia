// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"sort"

	"syslog"

	"netstack/fidlconv"
	"netstack/link"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
)

type stackImpl struct {
	ns *Netstack
}

func getInterfaceInfo(ifs *ifState, addresses []tcpip.ProtocolAddress) stack.InterfaceInfo {
	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	// TODO(tkilbourn): distinguish between enabled and link up
	administrativeStatus := stack.AdministrativeStatusDisabled
	physicalStatus := stack.PhysicalStatusDown
	if ifs.mu.state == link.StateStarted {
		administrativeStatus = stack.AdministrativeStatusEnabled
		physicalStatus = stack.PhysicalStatusUp
	}

	addrs := make([]stack.InterfaceAddress, 0, len(addresses))
	for _, a := range addresses {
		if a.Protocol != ipv4.ProtocolNumber && a.Protocol != ipv6.ProtocolNumber {
			continue
		}
		addrs = append(addrs, stack.InterfaceAddress{
			IpAddress: fidlconv.ToNetIpAddress(a.AddressWithPrefix.Address),
			PrefixLen: uint8(a.AddressWithPrefix.PrefixLen),
		})
	}

	var mac *ethernet.MacAddress
	var topopath string
	if eth := ifs.eth; eth != nil {
		mac = &ethernet.MacAddress{}
		copy(mac.Octets[:], ifs.endpoint.LinkAddress())
		topopath = eth.Path()
	}

	return stack.InterfaceInfo{
		Id: uint64(ifs.nicid),
		Properties: stack.InterfaceProperties{
			Name:                 ifs.ns.nameLocked(ifs.nicid),
			Topopath:             topopath,
			Filepath:             ifs.filepath,
			Mac:                  mac,
			Mtu:                  uint32(ifs.endpoint.MTU()),
			AdministrativeStatus: administrativeStatus,
			PhysicalStatus:       physicalStatus,
			Features:             ifs.features,
			Addresses:            addrs,
		},
	}
}

func (ns *Netstack) getNetInterfaces() []stack.InterfaceInfo {
	ns.mu.Lock()
	out := make([]stack.InterfaceInfo, 0, len(ns.mu.ifStates))
	for _, ifs := range ns.mu.ifStates {
		addresses := ns.getAddressesLocked(ifs.nicid)
		out = append(out, getInterfaceInfo(ifs, addresses))
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
	return nil, uint64(ifs.nicid)
}

func (ns *Netstack) delInterface(id uint64) *stack.Error {
	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if ok {
		if err := ifs.eth.Close(); err != nil {
			syslog.Errorf("ifs.eth.Close() failed (NIC: %d): %v", id, err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
		return nil
	}
	return &stack.Error{Type: stack.ErrorTypeNotFound}
}

func (ns *Netstack) getInterface(id uint64) (*stack.InterfaceInfo, *stack.Error) {
	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	if !ok {
		ns.mu.Unlock()
		return nil, &stack.Error{Type: stack.ErrorTypeNotFound}
	}
	addresses := ns.getAddressesLocked(ifs.nicid)
	interfaceInfo := getInterfaceInfo(ifs, addresses)
	ns.mu.Unlock()

	return &interfaceInfo, nil
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
			syslog.Errorf("ifs.eth.Up() failed (NIC %d): %v", id, err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
	} else {
		if err := ifs.eth.Down(); err != nil {
			syslog.Errorf("ifs.eth.Down() failed (NIC %d): %v", id, err)
			return &stack.Error{Type: stack.ErrorTypeInternal}
		}
	}
	return nil
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) *stack.Error {
	nicid := tcpip.NICID(id)

	ns.mu.Lock()
	_, ok := ns.mu.ifStates[nicid]
	ns.mu.Unlock()

	if !ok {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	var protocol tcpip.NetworkProtocolNumber
	switch typ := ifAddr.IpAddress.Which(); typ {
	case net.IpAddressIpv4:
		protocol = ipv4.ProtocolNumber
	case net.IpAddressIpv6:
		protocol = ipv6.ProtocolNumber
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", typ))
	}

	if err := ns.addInterfaceAddress(nicid, protocol, fidlconv.ToTCPIPAddress(ifAddr.IpAddress), ifAddr.PrefixLen); err != nil {
		syslog.Errorf("(*Netstack).setInterfaceAddress(...) failed (NIC %d): %v", nicid, err)
		return &stack.Error{Type: stack.ErrorTypeBadState}
	}
	return nil
}

func (ns *Netstack) delInterfaceAddr(id uint64, addr stack.InterfaceAddress) *stack.Error {
	ns.mu.Lock()
	_, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if !ok {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	var protocol tcpip.NetworkProtocolNumber
	switch tag := addr.IpAddress.Which(); tag {
	case net.IpAddressIpv4:
		protocol = ipv4.ProtocolNumber
	case net.IpAddressIpv6:
		protocol = ipv6.ProtocolNumber
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", tag))
	}

	if err := ns.removeInterfaceAddress(tcpip.NICID(id), protocol, fidlconv.ToTCPIPAddress(addr.IpAddress), uint8(addr.PrefixLen)); err != nil {
		syslog.Errorf("failed to remove interface address: %s", err)
		return &stack.Error{Type: stack.ErrorTypeInternal}
	}

	return nil
}

func (ns *Netstack) getForwardingTable() []stack.ForwardingEntry {
	ert := ns.GetExtendedRouteTable()
	entries := make([]stack.ForwardingEntry, 0, len(ert))
	for _, er := range ert {
		entries = append(entries, fidlconv.TcpipRouteToForwardingEntry(er.Route))
	}
	return entries
}

// validateSubnet returns true if the prefix length is valid and no
// address bits are set beyond the prefix length.
func validateSubnet(subnet net.Subnet) bool {
	var ipBytes []uint8
	switch typ := subnet.Addr.Which(); typ {
	case net.IpAddressIpv4:
		ipBytes = subnet.Addr.Ipv4.Addr[:]
	case net.IpAddressIpv6:
		ipBytes = subnet.Addr.Ipv6.Addr[:]
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", typ))
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

	if err := ns.AddRoute(fidlconv.ForwardingEntryToTcpipRoute(entry), metricNotSet, false /* not dynamic */); err != nil {
		syslog.Errorf("adding forwarding entry %+v to route table failed: %s", entry, err)
		return &stack.Error{Type: stack.ErrorTypeInvalidArgs}
	}

	return nil
}

func (ns *Netstack) delForwardingEntry(subnet net.Subnet) *stack.Error {
	if !validateSubnet(subnet) {
		return &stack.Error{Type: stack.ErrorTypeInvalidArgs}
	}

	sn, err := fidlconv.ToTCPIPSubnet(subnet)
	if err != nil {
		syslog.Errorf("cannot convert subnet %+v: %s", subnet, err)
		return &stack.Error{Type: stack.ErrorTypeInvalidArgs}
	}

	if err := ns.DelRoute(tcpip.Route{Destination: sn.ID(), Mask: sn.Mask()}); err != nil {
		syslog.Errorf("deleting forwarding entry %+v from route table failed: %s", subnet, err)
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	return nil
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

func (ni *stackImpl) DelInterfaceAddress(id uint64, addr stack.InterfaceAddress) (*stack.Error, error) {
	return ni.ns.delInterfaceAddr(id, addr), nil
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

func (ni *stackImpl) EnablePacketFilter(id uint64) (stack.StackEnablePacketFilterResult, error) {
	ni.ns.mu.Lock()
	ifs, ok := ni.ns.mu.ifStates[tcpip.NICID(id)]
	ni.ns.mu.Unlock()

	var result stack.StackEnablePacketFilterResult
	if !ok {
		result.SetErr(stack.ErrorTypeNotFound)
	} else if ifs.filterEndpoint == nil {
		result.SetErr(stack.ErrorTypeNotSupported)
	} else {
		ifs.filterEndpoint.Enable()
		result.SetResponse(stack.StackEnablePacketFilterResponse{})
	}
	return result, nil
}

func (ni *stackImpl) DisablePacketFilter(id uint64) (stack.StackDisablePacketFilterResult, error) {
	ni.ns.mu.Lock()
	ifs, ok := ni.ns.mu.ifStates[tcpip.NICID(id)]
	ni.ns.mu.Unlock()

	var result stack.StackDisablePacketFilterResult
	if !ok {
		result.SetErr(stack.ErrorTypeNotFound)
	} else if ifs.filterEndpoint == nil {
		result.SetErr(stack.ErrorTypeNotSupported)
	} else {
		ifs.filterEndpoint.Disable()
		result.SetResponse(stack.StackDisablePacketFilterResponse{})
	}
	return result, nil
}

func (ni *stackImpl) EnableIpForwarding() error {
	ni.ns.mu.Lock()
	ni.ns.mu.stack.SetForwarding(true)
	ni.ns.mu.Unlock()
	return nil
}

func (ni *stackImpl) DisableIpForwarding() error {
	ni.ns.mu.Lock()
	ni.ns.mu.stack.SetForwarding(false)
	ni.ns.mu.Unlock()
	return nil
}
