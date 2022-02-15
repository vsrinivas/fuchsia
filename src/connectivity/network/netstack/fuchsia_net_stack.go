// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"errors"
	"fmt"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/fidl"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"

	fidlethernet "fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

var _ stack.StackWithCtx = (*stackImpl)(nil)

type stackImpl struct {
	ns          *Netstack
	dnsWatchers *dnsServerWatcherCollection
}

func (ns *Netstack) delInterface(id uint64) stack.StackDelEthernetInterfaceResult {
	var result stack.StackDelEthernetInterfaceResult

	if nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]; ok {
		if nicInfo.Flags.Loopback {
			result.SetErr(stack.ErrorNotSupported)
		} else {
			nicInfo.Context.(*ifState).RemoveByUser()
			result.SetResponse(stack.StackDelEthernetInterfaceResponse{})
		}
	} else {
		result.SetErr(stack.ErrorNotFound)
	}

	return result
}

func (ns *Netstack) enableInterface(id uint64) stack.StackEnableInterfaceDeprecatedResult {
	var result stack.StackEnableInterfaceDeprecatedResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	if err := nicInfo.Context.(*ifState).Up(); err != nil {
		_ = syslog.Errorf("ifs.Up() failed (NIC %d): %s", id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}

	result.SetResponse(stack.StackEnableInterfaceDeprecatedResponse{})
	return result
}

func (ns *Netstack) disableInterface(id uint64) stack.StackDisableInterfaceDeprecatedResult {
	var result stack.StackDisableInterfaceDeprecatedResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	if err := nicInfo.Context.(*ifState).Down(); err != nil {
		_ = syslog.Errorf("ifs.Down() failed (NIC %d): %s", id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}

	result.SetResponse(stack.StackDisableInterfaceDeprecatedResponse{})
	return result
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr net.Subnet) stack.StackAddInterfaceAddressDeprecatedResult {
	var result stack.StackAddInterfaceAddressDeprecatedResult

	protocolAddr := fidlconv.ToTCPIPProtocolAddress(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	switch status := ns.addInterfaceAddress(tcpip.NICID(id), protocolAddr, true /* addRoute */); status {
	case zx.ErrOk:
		result.SetResponse(stack.StackAddInterfaceAddressDeprecatedResponse{})
		return result
	case zx.ErrNotFound:
		result.SetErr(stack.ErrorNotFound)
		return result
	case zx.ErrAlreadyExists:
		_ = syslog.Warnf("(*Netstack).addInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, status)
		result.SetErr(stack.ErrorAlreadyExists)
		return result
	default:
		panic(fmt.Sprintf("NIC %d: failed to add address %s: %s", id, protocolAddr.AddressWithPrefix, status))
	}
}

func (ns *Netstack) delInterfaceAddr(id uint64, ifAddr net.Subnet) stack.StackDelInterfaceAddressDeprecatedResult {
	protocolAddr := fidlconv.ToTCPIPProtocolAddress(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		return stack.StackDelInterfaceAddressDeprecatedResultWithErr(stack.ErrorInvalidArgs)
	}

	switch status := ns.removeInterfaceAddress(tcpip.NICID(id), protocolAddr, true /* removeRoute */); status {
	case zx.ErrOk:
		return stack.StackDelInterfaceAddressDeprecatedResultWithResponse(stack.StackDelInterfaceAddressDeprecatedResponse{})
	case zx.ErrNotFound:
		return stack.StackDelInterfaceAddressDeprecatedResultWithErr(stack.ErrorNotFound)
	default:
		_ = syslog.Errorf("(*Netstack).delInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, status)
		return stack.StackDelInterfaceAddressDeprecatedResultWithErr(stack.ErrorInternal)
	}
}

func (ns *Netstack) getForwardingTable() []stack.ForwardingEntry {
	ert := ns.GetExtendedRouteTable()
	entries := make([]stack.ForwardingEntry, 0, len(ert))
	for _, er := range ert {
		entry := fidlconv.TCPIPRouteToForwardingEntry(er.Route)
		entry.Metric = uint32(er.Metric)
		entries = append(entries, entry)
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
		return result
	}

	route := fidlconv.ForwardingEntryToTCPIPRoute(entry)
	if err := ns.AddRoute(route, routes.Metric(entry.Metric), false /* not dynamic */); err != nil {
		if errors.Is(err, routes.ErrNoSuchNIC) {
			result.SetErr(stack.ErrorInvalidArgs)
		} else {
			_ = syslog.Errorf("adding route %s to route table failed: %s", route, err)
			result.SetErr(stack.ErrorInternal)
		}
		return result
	}
	result.SetResponse(stack.StackAddForwardingEntryResponse{})
	return result
}

func (ns *Netstack) delForwardingEntry(entry stack.ForwardingEntry) stack.StackDelForwardingEntryResult {
	var result stack.StackDelForwardingEntryResult

	if !validateSubnet(entry.Subnet) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	route := fidlconv.ForwardingEntryToTCPIPRoute(entry)
	if err := ns.DelRoute(route); err != nil {
		if errors.Is(err, routes.ErrNoSuchRoute) {
			result.SetErr(stack.ErrorNotFound)
		} else {
			_ = syslog.Errorf("deleting route %s from route table failed: %s", route, err)
			result.SetErr(stack.ErrorInternal)
		}
		return result
	}
	result.SetResponse(stack.StackDelForwardingEntryResponse{})
	return result
}

func (ni *stackImpl) AddEthernetInterface(_ fidl.Context, topologicalPath string, device fidlethernet.DeviceWithCtxInterface) (stack.StackAddEthernetInterfaceResult, error) {
	var result stack.StackAddEthernetInterfaceResult
	if ifs, err := ni.ns.addEth(topologicalPath, netstack.InterfaceConfig{}, &device); err != nil {
		var tcpipErr *TcpIpError
		if errors.As(err, &tcpipErr) {
			result.SetErr(tcpipErr.ToStackError())
		} else {
			result.SetErr(stack.ErrorInternal)
		}
	} else {
		result.SetResponse(stack.StackAddEthernetInterfaceResponse{
			Id: uint64(ifs.nicid),
		})
	}
	return result, nil
}

func (ni *stackImpl) DelEthernetInterface(_ fidl.Context, id uint64) (stack.StackDelEthernetInterfaceResult, error) {
	return ni.ns.delInterface(id), nil
}

func (ni *stackImpl) EnableInterfaceDeprecated(_ fidl.Context, id uint64) (stack.StackEnableInterfaceDeprecatedResult, error) {
	return ni.ns.enableInterface(id), nil
}

func (ni *stackImpl) DisableInterfaceDeprecated(_ fidl.Context, id uint64) (stack.StackDisableInterfaceDeprecatedResult, error) {
	return ni.ns.disableInterface(id), nil
}

func (ni *stackImpl) AddInterfaceAddressDeprecated(_ fidl.Context, id uint64, addr net.Subnet) (stack.StackAddInterfaceAddressDeprecatedResult, error) {
	return ni.ns.addInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) DelInterfaceAddressDeprecated(_ fidl.Context, id uint64, addr net.Subnet) (stack.StackDelInterfaceAddressDeprecatedResult, error) {
	return ni.ns.delInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) GetForwardingTable(fidl.Context) ([]stack.ForwardingEntry, error) {
	return ni.ns.getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(_ fidl.Context, entry stack.ForwardingEntry) (stack.StackAddForwardingEntryResult, error) {
	return ni.ns.addForwardingEntry(entry), nil
}

func (ni *stackImpl) DelForwardingEntry(_ fidl.Context, entry stack.ForwardingEntry) (stack.StackDelForwardingEntryResult, error) {
	return ni.ns.delForwardingEntry(entry), nil
}

func (ni *stackImpl) EnableIpForwarding(fidl.Context) error {
	for _, protocol := range []tcpip.NetworkProtocolNumber{
		ipv4.ProtocolNumber,
		ipv6.ProtocolNumber,
	} {
		ni.ns.stack.SetForwardingDefaultAndAllNICs(protocol, true)
	}
	return nil
}

func (ni *stackImpl) DisableIpForwarding(fidl.Context) error {
	for _, protocol := range []tcpip.NetworkProtocolNumber{
		ipv4.ProtocolNumber,
		ipv6.ProtocolNumber,
	} {
		ni.ns.stack.SetForwardingDefaultAndAllNICs(protocol, false)
	}
	return nil
}

func (ni *stackImpl) GetInterfaceIpForwarding(_ fidl.Context, id uint64, ip net.IpVersion) (stack.StackGetInterfaceIpForwardingResult, error) {
	netProto, ok := fidlconv.ToTCPIPNetProto(ip)
	if !ok {
		return stack.StackGetInterfaceIpForwardingResultWithErr(stack.ErrorInvalidArgs), nil
	}

	switch enabled, err := ni.ns.stack.NICForwarding(tcpip.NICID(id), netProto); err.(type) {
	case nil:
		return stack.StackGetInterfaceIpForwardingResultWithResponse(stack.StackGetInterfaceIpForwardingResponse{Enabled: enabled}), nil
	case *tcpip.ErrUnknownNICID:
		return stack.StackGetInterfaceIpForwardingResultWithErr(stack.ErrorNotFound), nil
	default:
		panic(fmt.Sprintf("ni.ns.stack.SetNICForwarding(tcpip.NICID(%d), %d, %t): %s", id, netProto, enabled, err))
	}
}

func (ni *stackImpl) SetInterfaceIpForwarding(_ fidl.Context, id uint64, ip net.IpVersion, enabled bool) (stack.StackSetInterfaceIpForwardingResult, error) {
	netProto, ok := fidlconv.ToTCPIPNetProto(ip)
	if !ok {
		return stack.StackSetInterfaceIpForwardingResultWithErr(stack.ErrorInvalidArgs), nil
	}

	switch err := ni.ns.stack.SetNICForwarding(tcpip.NICID(id), netProto, enabled); err.(type) {
	case nil:
		return stack.StackSetInterfaceIpForwardingResultWithResponse(stack.StackSetInterfaceIpForwardingResponse{}), nil
	case *tcpip.ErrUnknownNICID:
		return stack.StackSetInterfaceIpForwardingResultWithErr(stack.ErrorNotFound), nil
	default:
		panic(fmt.Sprintf("ni.ns.stack.SetNICForwarding(tcpip.NICID(%d), %d, %t): %s", id, netProto, enabled, err))
	}
}

func (ni *stackImpl) GetDnsServerWatcher(ctx_ fidl.Context, watcher name.DnsServerWatcherWithCtxInterfaceRequest) error {
	return ni.dnsWatchers.Bind(watcher)
}

var _ stack.LogWithCtx = (*logImpl)(nil)

type logImpl struct {
	logPackets *uint32
}

func (li *logImpl) SetLogPackets(_ fidl.Context, enabled bool) error {
	var val uint32
	if enabled {
		val = 1
	}
	atomic.StoreUint32(li.logPackets, val)
	syslog.VLogTf(syslog.DebugVerbosity, "fuchsia_net_stack", "SetLogPackets: %t", enabled)
	return nil
}
