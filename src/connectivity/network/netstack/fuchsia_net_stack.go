// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"errors"
	"fmt"
	"sort"
	"sync/atomic"
	"syscall/zx/fidl"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/netdevice"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/logger"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	tcpipstack "gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.StackWithCtx = (*stackImpl)(nil)

type stackImpl struct {
	ns          *Netstack
	dnsWatchers *dnsServerWatcherCollection
}

func getInterfaceInfo(nicInfo tcpipstack.NICInfo) stack.InterfaceInfo {
	ifs := nicInfo.Context.(*ifState)
	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	administrativeStatus := stack.AdministrativeStatusDisabled
	if ifs.mu.adminUp {
		administrativeStatus = stack.AdministrativeStatusEnabled
	}
	physicalStatus := stack.PhysicalStatusDown
	if ifs.LinkOnlineLocked() {
		physicalStatus = stack.PhysicalStatusUp
	}

	addrs := make([]net.Subnet, 0, len(nicInfo.ProtocolAddresses))
	for _, a := range nicInfo.ProtocolAddresses {
		if a.Protocol != ipv4.ProtocolNumber && a.Protocol != ipv6.ProtocolNumber {
			continue
		}
		addrs = append(addrs, net.Subnet{
			Addr:      fidlconv.ToNetIpAddress(a.AddressWithPrefix.Address),
			PrefixLen: uint8(a.AddressWithPrefix.PrefixLen),
		})
	}

	var features ethernet.Features
	if ifs.endpoint.Capabilities()&tcpipstack.CapabilityLoopback != 0 {
		features |= ethernet.FeaturesLoopback
	}

	var topopath, filepath string
	if client, ok := ifs.controller.(*eth.Client); ok {
		topopath = client.Topopath()
		filepath = client.Filepath()
		features |= client.Info.Features
	}

	mac := &ethernet.MacAddress{}
	copy(mac.Octets[:], ifs.endpoint.LinkAddress())

	return stack.InterfaceInfo{
		Id: uint64(ifs.nicid),
		Properties: stack.InterfaceProperties{
			Name:                 nicInfo.Name,
			Topopath:             topopath,
			Filepath:             filepath,
			Mac:                  mac,
			Mtu:                  ifs.endpoint.MTU(),
			AdministrativeStatus: administrativeStatus,
			PhysicalStatus:       physicalStatus,
			Features:             features,
			Addresses:            addrs,
		},
	}
}

func (ns *Netstack) getNetInterfaces() []stack.InterfaceInfo {
	nicInfos := ns.stack.NICInfo()
	out := make([]stack.InterfaceInfo, 0, len(nicInfos))
	for _, nicInfo := range nicInfos {
		out = append(out, getInterfaceInfo(nicInfo))
	}

	sort.Slice(out, func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ns *Netstack) addInterface(config stack.InterfaceConfig, device stack.DeviceDefinition) stack.StackAddInterfaceResult {

	var (
		namePrefix string
		ep         tcpipstack.LinkEndpoint
		controller link.Controller
		observer   link.Observer
	)

	switch device.Which() {
	case stack.DeviceDefinitionEthernet:
		client, err := netdevice.NewMacAddressingClient(context.Background(), &device.Ethernet.NetworkDevice, &device.Ethernet.Mac, &netdevice.SimpleSessionConfigFactory{
			FrameTypes: []network.FrameType{network.FrameTypeEthernet},
		})
		if err != nil {
			_ = syslog.Warnf("failed to create network device client for Ethernet interface: %s", err)
			return stack.StackAddInterfaceResultWithErr(stack.ErrorInternal)
		}
		ep = eth.NewLinkEndpoint(client)
		controller = client
		observer = client
		namePrefix = "eth"
	case stack.DeviceDefinitionIp:
		client, err := netdevice.NewClient(context.Background(), &device.Ip, &netdevice.SimpleSessionConfigFactory{
			FrameTypes: []network.FrameType{network.FrameTypeIpv4, network.FrameTypeIpv6},
		})
		if err != nil {
			_ = syslog.Warnf("failed to create network device client for IP interface: %s", err)
			return stack.StackAddInterfaceResultWithErr(stack.ErrorInternal)
		}
		ep = client
		controller = client
		observer = client
		namePrefix = "ip"
	default:
		_ = syslog.Errorf("unsupported device definition: %d", device.Which())
		return stack.StackAddInterfaceResultWithErr(stack.ErrorInvalidArgs)
	}

	ifs, err := ns.addEndpoint(
		makeEndpointName(namePrefix, config.GetNameWithDefault("")),
		ep,
		controller,
		observer,
		true, /* doFilter */
		routes.Metric(config.GetMetricWithDefault(0)),
	)
	if err != nil {
		var tcpipError TcpIpError
		if errors.As(err, &tcpipError) {
			return stack.StackAddInterfaceResultWithErr(tcpipError.ToStackError())
		} else {
			return stack.StackAddInterfaceResultWithErr(stack.ErrorInternal)
		}
	}
	return stack.StackAddInterfaceResultWithResponse(stack.StackAddInterfaceResponse{Id: uint64(ifs.nicid)})
}

func (ns *Netstack) delInterface(id uint64) stack.StackDelEthernetInterfaceResult {
	var result stack.StackDelEthernetInterfaceResult

	if nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]; ok {
		nicInfo.Context.(*ifState).Remove()
		result.SetResponse(stack.StackDelEthernetInterfaceResponse{})
	} else {
		result.SetErr(stack.ErrorNotFound)
	}

	return result
}

func (ns *Netstack) getInterface(id uint64) stack.StackGetInterfaceInfoResult {
	var result stack.StackGetInterfaceInfoResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	result.SetResponse(stack.StackGetInterfaceInfoResponse{
		Info: getInterfaceInfo(nicInfo),
	})
	return result
}

func (ns *Netstack) enableInterface(id uint64) stack.StackEnableInterfaceResult {
	var result stack.StackEnableInterfaceResult

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

	result.SetResponse(stack.StackEnableInterfaceResponse{})
	return result
}

func (ns *Netstack) disableInterface(id uint64) stack.StackDisableInterfaceResult {
	var result stack.StackDisableInterfaceResult

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

	result.SetResponse(stack.StackDisableInterfaceResponse{})
	return result
}

func toProtocolAddr(ifAddr net.Subnet) tcpip.ProtocolAddress {
	protocolAddr := tcpip.ProtocolAddress{
		AddressWithPrefix: tcpip.AddressWithPrefix{
			PrefixLen: int(ifAddr.PrefixLen),
		},
	}

	switch typ := ifAddr.Addr.Which(); typ {
	case net.IpAddressIpv4:
		protocolAddr.Protocol = ipv4.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(ifAddr.Addr.Ipv4.Addr[:])
	case net.IpAddressIpv6:
		protocolAddr.Protocol = ipv6.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(ifAddr.Addr.Ipv6.Addr[:])
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", typ))
	}
	return protocolAddr
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr net.Subnet) stack.StackAddInterfaceAddressResult {
	var result stack.StackAddInterfaceAddressResult

	protocolAddr := toProtocolAddr(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	found, err := ns.addInterfaceAddress(tcpip.NICID(id), protocolAddr)
	if err != nil {
		syslog.Errorf("(*Netstack).addInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, err)
		result.SetErr(stack.ErrorBadState)
		return result
	}

	if !found {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	result.SetResponse(stack.StackAddInterfaceAddressResponse{})
	return result
}

func (ns *Netstack) delInterfaceAddr(id uint64, ifAddr net.Subnet) stack.StackDelInterfaceAddressResult {
	var result stack.StackDelInterfaceAddressResult

	protocolAddr := toProtocolAddr(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	found, err := ns.removeInterfaceAddress(tcpip.NICID(id), protocolAddr)
	if err != nil {
		syslog.Errorf("(*Netstack).delInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}
	if !found {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	result.SetResponse(stack.StackDelInterfaceAddressResponse{})
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
		return result
	}

	route := fidlconv.ForwardingEntryToTcpipRoute(entry)
	if err := ns.AddRoute(route, metricNotSet, false /* not dynamic */); err != nil {
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

func (ns *Netstack) delForwardingEntry(subnet net.Subnet) stack.StackDelForwardingEntryResult {
	var result stack.StackDelForwardingEntryResult

	if !validateSubnet(subnet) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	destination := fidlconv.ToTCPIPSubnet(subnet)
	if err := ns.DelRoute(tcpip.Route{Destination: destination}); err != nil {
		if errors.Is(err, routes.ErrNoSuchRoute) {
			result.SetErr(stack.ErrorNotFound)
		} else {
			_ = syslog.Errorf("deleting destination %s from route table failed: %s", destination, err)
			result.SetErr(stack.ErrorInternal)
		}
		return result
	}
	result.SetResponse(stack.StackDelForwardingEntryResponse{})
	return result
}

func (ni *stackImpl) AddEthernetInterface(_ fidl.Context, topologicalPath string, device ethernet.DeviceWithCtxInterface) (stack.StackAddEthernetInterfaceResult, error) {
	var result stack.StackAddEthernetInterfaceResult
	if ifs, err := ni.ns.addEth(topologicalPath, netstack.InterfaceConfig{}, &device); err != nil {
		var tcpipErr TcpIpError
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

func (ni *stackImpl) AddInterface(_ fidl.Context, config stack.InterfaceConfig, device stack.DeviceDefinition) (stack.StackAddInterfaceResult, error) {
	return ni.ns.addInterface(config, device), nil
}

func (ni *stackImpl) DelEthernetInterface(_ fidl.Context, id uint64) (stack.StackDelEthernetInterfaceResult, error) {
	return ni.ns.delInterface(id), nil
}

func (ni *stackImpl) ListInterfaces(fidl.Context) ([]stack.InterfaceInfo, error) {
	return ni.ns.getNetInterfaces(), nil
}

func (ni *stackImpl) GetInterfaceInfo(_ fidl.Context, id uint64) (stack.StackGetInterfaceInfoResult, error) {
	return ni.ns.getInterface(id), nil
}

func (ni *stackImpl) EnableInterface(_ fidl.Context, id uint64) (stack.StackEnableInterfaceResult, error) {
	return ni.ns.enableInterface(id), nil
}

func (ni *stackImpl) DisableInterface(_ fidl.Context, id uint64) (stack.StackDisableInterfaceResult, error) {
	return ni.ns.disableInterface(id), nil
}

func (ni *stackImpl) AddInterfaceAddress(_ fidl.Context, id uint64, addr net.Subnet) (stack.StackAddInterfaceAddressResult, error) {
	return ni.ns.addInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) DelInterfaceAddress(_ fidl.Context, id uint64, addr net.Subnet) (stack.StackDelInterfaceAddressResult, error) {
	return ni.ns.delInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) GetForwardingTable(fidl.Context) ([]stack.ForwardingEntry, error) {
	return ni.ns.getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(_ fidl.Context, entry stack.ForwardingEntry) (stack.StackAddForwardingEntryResult, error) {
	return ni.ns.addForwardingEntry(entry), nil
}

func (ni *stackImpl) DelForwardingEntry(_ fidl.Context, subnet net.Subnet) (stack.StackDelForwardingEntryResult, error) {
	return ni.ns.delForwardingEntry(subnet), nil
}

func (ni *stackImpl) EnablePacketFilter(_ fidl.Context, id uint64) (stack.StackEnablePacketFilterResult, error) {
	var result stack.StackEnablePacketFilterResult

	nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result, nil
	}

	filter := nicInfo.Context.(*ifState).filterEndpoint
	if filter == nil {
		result.SetErr(stack.ErrorNotSupported)
		return result, nil
	}

	filter.Enable()

	result.SetResponse(stack.StackEnablePacketFilterResponse{})
	return result, nil
}

func (ni *stackImpl) DisablePacketFilter(_ fidl.Context, id uint64) (stack.StackDisablePacketFilterResult, error) {
	var result stack.StackDisablePacketFilterResult

	nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result, nil
	}

	filter := nicInfo.Context.(*ifState).filterEndpoint
	if filter == nil {
		result.SetErr(stack.ErrorNotSupported)
		return result, nil
	}

	filter.Disable()

	result.SetResponse(stack.StackDisablePacketFilterResponse{})
	return result, nil
}

func (ni *stackImpl) EnableIpForwarding(fidl.Context) error {
	for _, protocol := range []tcpip.NetworkProtocolNumber{
		ipv4.ProtocolNumber,
		ipv6.ProtocolNumber,
	} {
		ni.ns.stack.SetForwarding(protocol, true)
	}
	return nil
}

func (ni *stackImpl) DisableIpForwarding(fidl.Context) error {
	for _, protocol := range []tcpip.NetworkProtocolNumber{
		ipv4.ProtocolNumber,
		ipv6.ProtocolNumber,
	} {
		ni.ns.stack.SetForwarding(protocol, false)
	}
	return nil
}

func (ni *stackImpl) GetDnsServerWatcher(ctx_ fidl.Context, watcher name.DnsServerWatcherWithCtxInterfaceRequest) error {
	return ni.dnsWatchers.Bind(watcher)
}

var _ stack.LogWithCtx = (*logImpl)(nil)

type logImpl struct {
	logger     *syslog.Logger
	logPackets *uint32
}

func (li *logImpl) SetLogLevel(_ fidl.Context, level logger.LogLevelFilter) (stack.LogSetLogLevelResult, error) {
	li.logger.SetSeverity(syslog.LogLevel(level))
	syslog.VLogTf(syslog.DebugVerbosity, "fuchsia_net_stack", "SetLogLevel: %s", level)
	return stack.LogSetLogLevelResultWithResponse(stack.LogSetLogLevelResponse{}), nil
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
