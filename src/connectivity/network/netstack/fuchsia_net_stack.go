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

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
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
	nicInfos := ns.mu.stack.NICInfo()
	for _, ifs := range ns.mu.ifStates {
		nicInfo, ok := nicInfos[ifs.nicid]
		if !ok {
			panic(fmt.Sprintf("NIC [%d] not found in %+v", ifs.nicid, nicInfo))
		}
		out = append(out, getInterfaceInfo(ifs, nicInfo.ProtocolAddresses))
	}
	ns.mu.Unlock()

	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ns *Netstack) addInterface(topologicalPath string, device ethernet.DeviceInterface) stack.StackAddEthernetInterfaceResult {
	var result stack.StackAddEthernetInterfaceResult

	ifs, err := ns.addEth(topologicalPath, netstack.InterfaceConfig{}, &device)
	if err != nil {
		result.SetErr(stack.ErrorInternal)
	} else {
		result.SetResponse(stack.StackAddEthernetInterfaceResponse{
			Id: uint64(ifs.nicid),
		})
	}
	return result
}

func (ns *Netstack) delInterface(id uint64) stack.StackDelEthernetInterfaceResult {
	var result stack.StackDelEthernetInterfaceResult

	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if !ok {
		result.SetErr(stack.ErrorNotFound)
	} else if err := ifs.eth.Close(); err != nil {
		syslog.Errorf("ifs.eth.Close() failed (NIC: %d): %v", id, err)
		result.SetErr(stack.ErrorInternal)
	} else {
		result.SetResponse(stack.StackDelEthernetInterfaceResponse{})
	}
	return result
}

func (ns *Netstack) getInterface(id uint64) stack.StackGetInterfaceInfoResult {
	var result stack.StackGetInterfaceInfoResult

	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
	} else {
		nicInfo, ok := ns.mu.stack.NICInfo()[ifs.nicid]
		if !ok {
			panic(fmt.Sprintf("NIC [%d] not found in %+v", ifs.nicid, nicInfo))
		}
		result.SetResponse(stack.StackGetInterfaceInfoResponse{
			Info: getInterfaceInfo(ifs, nicInfo.ProtocolAddresses),
		})
	}
	ns.mu.Unlock()

	return result
}

func (ns *Netstack) enableInterface(id uint64) stack.StackEnableInterfaceResult {
	var result stack.StackEnableInterfaceResult

	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if !ok {
		result.SetErr(stack.ErrorNotFound)
	} else if err := ifs.eth.Up(); err != nil {
		syslog.Errorf("ifs.eth.Up() failed (NIC %d): %s", id, err)
		result.SetErr(stack.ErrorInternal)
	} else {
		result.SetResponse(stack.StackEnableInterfaceResponse{})
	}
	return result
}

func (ns *Netstack) disableInterface(id uint64) stack.StackDisableInterfaceResult {
	var result stack.StackDisableInterfaceResult

	ns.mu.Lock()
	ifs, ok := ns.mu.ifStates[tcpip.NICID(id)]
	ns.mu.Unlock()

	if !ok {
		result.SetErr(stack.ErrorNotFound)
	} else if err := ifs.eth.Down(); err != nil {
		syslog.Errorf("ifs.eth.Down() failed (NIC %d): %s", id, err)
		result.SetErr(stack.ErrorInternal)
	} else {
		result.SetResponse(stack.StackDisableInterfaceResponse{})
	}
	return result
}

func toProtocolAddr(ifAddr stack.InterfaceAddress) tcpip.ProtocolAddress {
	protocolAddr := tcpip.ProtocolAddress{
		AddressWithPrefix: tcpip.AddressWithPrefix{
			PrefixLen: int(ifAddr.PrefixLen),
		},
	}

	switch typ := ifAddr.IpAddress.Which(); typ {
	case net.IpAddressIpv4:
		protocolAddr.Protocol = ipv4.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(ifAddr.IpAddress.Ipv4.Addr[:])
	case net.IpAddressIpv6:
		protocolAddr.Protocol = ipv6.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(ifAddr.IpAddress.Ipv6.Addr[:])
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", typ))
	}
	return protocolAddr
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) stack.StackAddInterfaceAddressResult {
	var result stack.StackAddInterfaceAddressResult

	protocolAddr := toProtocolAddr(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
	} else {
		found, err := ns.addInterfaceAddress(tcpip.NICID(id), protocolAddr)
		if err != nil {
			syslog.Errorf("(*Netstack).addInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, err)
			result.SetErr(stack.ErrorBadState)
		} else if !found {
			result.SetErr(stack.ErrorNotFound)
		} else {
			result.SetResponse(stack.StackAddInterfaceAddressResponse{})
		}
	}
	return result
}

func (ns *Netstack) delInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) stack.StackDelInterfaceAddressResult {
	var result stack.StackDelInterfaceAddressResult

	protocolAddr := toProtocolAddr(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
	} else {
		found, err := ns.removeInterfaceAddress(tcpip.NICID(id), protocolAddr)
		if err != nil {
			syslog.Errorf("(*Netstack).delInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, err)
			result.SetErr(stack.ErrorInternal)
		} else if !found {
			result.SetErr(stack.ErrorNotFound)
		} else {
			result.SetResponse(stack.StackDelInterfaceAddressResponse{})
		}
	}
	return result
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

func (ns *Netstack) addForwardingEntry(entry stack.ForwardingEntry) stack.StackAddForwardingEntryResult {
	var result stack.StackAddForwardingEntryResult

	if !validateSubnet(entry.Subnet) {
		result.SetErr(stack.ErrorInvalidArgs)
	} else if err := ns.AddRoute(fidlconv.ForwardingEntryToTcpipRoute(entry), metricNotSet, false /* not dynamic */); err != nil {
		syslog.Errorf("adding forwarding entry %+v to route table failed: %s", entry, err)
		result.SetErr(stack.ErrorInvalidArgs)
	} else {
		result.SetResponse(stack.StackAddForwardingEntryResponse{})
	}
	return result
}

func (ns *Netstack) delForwardingEntry(subnet net.Subnet) stack.StackDelForwardingEntryResult {
	var result stack.StackDelForwardingEntryResult

	if !validateSubnet(subnet) {
		result.SetErr(stack.ErrorInvalidArgs)
	} else if err := ns.DelRoute(tcpip.Route{Destination: fidlconv.ToTCPIPSubnet(subnet)}); err != nil {
		syslog.Errorf("deleting forwarding entry %+v from route table failed: %s", subnet, err)
		result.SetErr(stack.ErrorNotFound)
	} else {
		result.SetResponse(stack.StackDelForwardingEntryResponse{})
	}
	return result
}

func (ni *stackImpl) AddEthernetInterface(topologicalPath string, device ethernet.DeviceInterface) (stack.StackAddEthernetInterfaceResult, error) {
	return ni.ns.addInterface(topologicalPath, device), nil
}

func (ni *stackImpl) DelEthernetInterface(id uint64) (stack.StackDelEthernetInterfaceResult, error) {
	return ni.ns.delInterface(id), nil
}

func (ni *stackImpl) ListInterfaces() ([]stack.InterfaceInfo, error) {
	return ni.ns.getNetInterfaces(), nil
}

func (ni *stackImpl) GetInterfaceInfo(id uint64) (stack.StackGetInterfaceInfoResult, error) {
	return ni.ns.getInterface(id), nil
}

func (ni *stackImpl) EnableInterface(id uint64) (stack.StackEnableInterfaceResult, error) {
	return ni.ns.enableInterface(id), nil
}

func (ni *stackImpl) DisableInterface(id uint64) (stack.StackDisableInterfaceResult, error) {
	return ni.ns.disableInterface(id), nil
}

func (ni *stackImpl) AddInterfaceAddress(id uint64, addr stack.InterfaceAddress) (stack.StackAddInterfaceAddressResult, error) {
	return ni.ns.addInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) DelInterfaceAddress(id uint64, addr stack.InterfaceAddress) (stack.StackDelInterfaceAddressResult, error) {
	return ni.ns.delInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) GetForwardingTable() ([]stack.ForwardingEntry, error) {
	return ni.ns.getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(entry stack.ForwardingEntry) (stack.StackAddForwardingEntryResult, error) {
	return ni.ns.addForwardingEntry(entry), nil
}

func (ni *stackImpl) DelForwardingEntry(subnet net.Subnet) (stack.StackDelForwardingEntryResult, error) {
	return ni.ns.delForwardingEntry(subnet), nil
}

func (ni *stackImpl) EnablePacketFilter(id uint64) (stack.StackEnablePacketFilterResult, error) {
	ni.ns.mu.Lock()
	ifs, ok := ni.ns.mu.ifStates[tcpip.NICID(id)]
	ni.ns.mu.Unlock()

	var result stack.StackEnablePacketFilterResult
	if !ok {
		result.SetErr(stack.ErrorNotFound)
	} else if ifs.filterEndpoint == nil {
		result.SetErr(stack.ErrorNotSupported)
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
		result.SetErr(stack.ErrorNotFound)
	} else if ifs.filterEndpoint == nil {
		result.SetErr(stack.ErrorNotSupported)
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

type logImpl struct {
	logger *syslog.Logger
}

func (li *logImpl) SetLogLevel(level stack.LogLevelFilter) (stack.LogSetLogLevelResult, error) {
	li.logger.SetSeverity(syslog.LogLevel(level))
	syslog.VLogTf(syslog.DebugVerbosity, "fuchsia_net_stack", "SetSyslogLevel: %s", level)
	return stack.LogSetLogLevelResultWithResponse(stack.LogSetLogLevelResponse{}), nil
}
