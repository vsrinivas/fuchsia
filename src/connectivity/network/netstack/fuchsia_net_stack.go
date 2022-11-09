// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"errors"
	"fmt"
	"syscall/zx/fidl"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"

	fidlethernet "fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/atomicbitops"
	"gvisor.dev/gvisor/pkg/tcpip"
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
	if !validateSubnet(entry.Subnet) {
		return stack.StackDelForwardingEntryResultWithErr(stack.ErrorInvalidArgs)
	}

	route := fidlconv.ForwardingEntryToTCPIPRoute(entry)
	if routesDeleted := ns.DelRoute(route); len(routesDeleted) == 0 {
		return stack.StackDelForwardingEntryResultWithErr(stack.ErrorNotFound)
	}
	return stack.StackDelForwardingEntryResultWithResponse(stack.StackDelForwardingEntryResponse{})
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

func (ni *stackImpl) GetForwardingTable(fidl.Context) ([]stack.ForwardingEntry, error) {
	return ni.ns.getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(_ fidl.Context, entry stack.ForwardingEntry) (stack.StackAddForwardingEntryResult, error) {
	return ni.ns.addForwardingEntry(entry), nil
}

func (ni *stackImpl) DelForwardingEntry(_ fidl.Context, entry stack.ForwardingEntry) (stack.StackDelForwardingEntryResult, error) {
	return ni.ns.delForwardingEntry(entry), nil
}

// TODO(https://fxbug.dev/94475): Remove this.
func (ni *stackImpl) SetInterfaceIpForwardingDeprecated(_ fidl.Context, id uint64, ip net.IpVersion, enabled bool) (stack.StackSetInterfaceIpForwardingDeprecatedResult, error) {
	netProto, ok := fidlconv.ToTCPIPNetProto(ip)
	if !ok {
		return stack.StackSetInterfaceIpForwardingDeprecatedResultWithErr(stack.ErrorInvalidArgs), nil
	}

	nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		return stack.StackSetInterfaceIpForwardingDeprecatedResultWithErr(stack.ErrorNotFound), nil
	}

	// Lock the interface to synchronize changes with
	// fuchsia.net.interfaces.admin/Control.{Set,Get}Configuration.
	ifs := nicInfo.Context.(*ifState)
	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	// Invalidate all clients' destination caches, as disabling forwarding may
	// cause an existing cached route to become invalid.
	ifs.ns.resetDestinationCache()

	// We ignore the returned previous forwarding configuration as this FIDL
	// method has no use for it.
	switch _, err := ni.ns.stack.SetNICForwarding(tcpip.NICID(id), netProto, enabled); err.(type) {
	case nil:
		return stack.StackSetInterfaceIpForwardingDeprecatedResultWithResponse(stack.StackSetInterfaceIpForwardingDeprecatedResponse{}), nil
	case *tcpip.ErrUnknownNICID:
		return stack.StackSetInterfaceIpForwardingDeprecatedResultWithErr(stack.ErrorNotFound), nil
	default:
		panic(fmt.Sprintf("ni.ns.stack.SetNICForwarding(tcpip.NICID(%d), %d, %t): %s", id, netProto, enabled, err))
	}
}

func (ni *stackImpl) GetDnsServerWatcher(ctx_ fidl.Context, watcher name.DnsServerWatcherWithCtxInterfaceRequest) error {
	return ni.dnsWatchers.Bind(watcher)
}

func (ni *stackImpl) SetDhcpClientEnabled(ctx_ fidl.Context, id uint64, enable bool) (stack.StackSetDhcpClientEnabledResult, error) {
	var r stack.StackSetDhcpClientEnabledResult

	nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		return stack.StackSetDhcpClientEnabledResultWithErr(stack.ErrorNotFound), nil
	}

	ifState := nicInfo.Context.(*ifState)
	ifState.setDHCPStatus(nicInfo.Name, enable)

	r.SetResponse(stack.StackSetDhcpClientEnabledResponse{})
	return r, nil
}

var _ stack.LogWithCtx = (*logImpl)(nil)

type logImpl struct {
	logPackets *atomicbitops.Uint32
}

func (li *logImpl) SetLogPackets(_ fidl.Context, enabled bool) error {
	var val uint32
	if enabled {
		val = 1
	}
	li.logPackets.Store(val)
	syslog.VLogTf(syslog.DebugVerbosity, "fuchsia_net_stack", "SetLogPackets: %t", enabled)
	return nil
}
