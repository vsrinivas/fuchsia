// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"reflect"
	"sort"
	"strconv"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/netdevice"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	inspect "fidl/fuchsia/inspect/deprecated"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

// An infallible version of fuchsia.inspect.Inspect with FIDL details omitted.
type inspectInner interface {
	ReadData() inspect.Object
	ListChildren() []string
	GetChild(string) inspectInner
}

const (
	statsLabel    = "Stats"
	socketInfo    = "Socket Info"
	dhcpInfo      = "DHCP Info"
	ethInfo       = "Ethernet Info"
	netdeviceInfo = "Network Device Info"
	rxReads       = "RxReads"
	rxWrites      = "RxWrites"
	txReads       = "TxReads"
	txWrites      = "TxWrites"
)

// An adapter that implements fuchsia.inspect.InspectWithCtx using the above.

var _ inspect.InspectWithCtx = (*inspectImpl)(nil)

type inspectImpl struct {
	inner inspectInner
}

func (impl *inspectImpl) ReadData(fidl.Context) (inspect.Object, error) {
	return impl.inner.ReadData(), nil
}

func (impl *inspectImpl) ListChildren(fidl.Context) ([]string, error) {
	return impl.inner.ListChildren(), nil
}

func (impl *inspectImpl) OpenChild(ctx fidl.Context, childName string, childChannel inspect.InspectWithCtxInterfaceRequest) (bool, error) {
	if child := impl.inner.GetChild(childName); child != nil {
		svc := (&inspectImpl{
			inner: child,
		}).asService()
		return true, svc.AddFn(ctx, childChannel.Channel)
	}
	return false, nil
}

func (impl *inspectImpl) asService() *component.Service {
	stub := inspect.InspectWithCtxStub{Impl: impl}
	return &component.Service{
		AddFn: func(ctx fidl.Context, c zx.Channel) error {
			go component.ServeExclusive(ctx, &stub, c, func(err error) {
				_ = syslog.WarnTf(inspect.InspectName, "%s", err)
			})
			return nil
		},
	}
}

// Inspect implementations are exposed as directories containing a node called "inspect".

var _ component.Directory = (*inspectDirectory)(nil)

type inspectDirectory struct {
	asService func() *component.Service
}

func (dir *inspectDirectory) Get(name string) (component.Node, bool) {
	if name == inspect.InspectName {
		return dir.asService(), true
	}
	return nil, false
}

func (dir *inspectDirectory) ForEach(fn func(string, component.Node)) {
	fn(inspect.InspectName, dir.asService())
}

// statCounter enables *tcpip.StatCounters and other types in this
// package to be accessed via the same interface.
type statCounter interface {
	Value() uint64
}

var _ statCounter = (*tcpip.StatCounter)(nil)
var statCounterType = reflect.TypeOf((*statCounter)(nil)).Elem()

// Recursive reflection-based implementation for structs containing only other
// structs and stat counters.

var _ inspectInner = (*statCounterInspectImpl)(nil)

type statCounterInspectImpl struct {
	name  string
	value reflect.Value
}

func (impl *statCounterInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name:    impl.name,
		Metrics: impl.asMetrics(),
	}
}

func (impl *statCounterInspectImpl) asMetrics() []inspect.Metric {
	var metrics []inspect.Metric
	typ := impl.value.Type()
	for i := 0; i < impl.value.NumField(); i++ {
		// PkgPath is empty for exported field names.
		if field := typ.Field(i); len(field.PkgPath) == 0 {
			v := impl.value.Field(i)
			counter, ok := v.Interface().(statCounter)
			if !ok && v.CanAddr() {
				counter, ok = v.Addr().Interface().(statCounter)
			}
			if ok {
				metrics = append(metrics, inspect.Metric{
					Key:   field.Name,
					Value: inspect.MetricValueWithUintValue(counter.Value()),
				})
			} else if field.Anonymous && v.Kind() == reflect.Struct {
				metrics = append(metrics, (&statCounterInspectImpl{value: v}).asMetrics()...)
			}
		}
	}
	return metrics
}

func (impl *statCounterInspectImpl) ListChildren() []string {
	var children []string
	typ := impl.value.Type()
	for i := 0; i < impl.value.NumField(); i++ {
		// PkgPath is empty for exported field names.
		//
		// Avoid inspecting any field that implements statCounter.
		if field := typ.Field(i); len(field.PkgPath) == 0 && field.Type.Kind() == reflect.Struct && !field.Type.Implements(statCounterType) && !reflect.PtrTo(field.Type).Implements(statCounterType) {
			if field.Anonymous {
				children = append(children, (&statCounterInspectImpl{value: impl.value.Field(i)}).ListChildren()...)
			} else {
				children = append(children, field.Name)
			}
		}
	}
	return children
}

func (impl *statCounterInspectImpl) GetChild(childName string) inspectInner {
	if typ, ok := impl.value.Type().FieldByName(childName); ok {
		if len(typ.PkgPath) == 0 && typ.Type.Kind() == reflect.Struct {
			// PkgPath is empty for exported field names.
			if child := impl.value.FieldByName(childName); child.IsValid() {
				return &statCounterInspectImpl{
					name:  childName,
					value: child,
				}
			}
		}
	}
	return nil
}

var _ inspectInner = (*nicInfoMapInspectImpl)(nil)

// Picking info to inspect from ifState in netstack.
type ifStateInfo struct {
	stack.NICInfo
	nicid               tcpip.NICID
	adminUp, linkOnline bool
	dnsServers          []tcpip.Address
	dhcpEnabled         bool
	dhcpInfo            dhcp.Info
	dhcpStats           *dhcp.Stats
	controller          link.Controller
}

type nicInfoMapInspectImpl struct {
	value map[tcpip.NICID]ifStateInfo
}

func (impl *nicInfoMapInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: "NICs",
	}
}

func (impl *nicInfoMapInspectImpl) ListChildren() []string {
	var children []string
	for nicID := range impl.value {
		children = append(children, strconv.FormatUint(uint64(nicID), 10))
	}
	sort.Strings(children)
	return children
}

func (impl *nicInfoMapInspectImpl) GetChild(childName string) inspectInner {
	id, err := strconv.ParseInt(childName, 10, 32)
	if err != nil {
		syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName, "OpenChild: %s", err)
		return nil
	}
	if child, ok := impl.value[tcpip.NICID(id)]; ok {
		return &nicInfoInspectImpl{
			name:  childName,
			value: child,
		}
	}
	return nil
}

var _ inspectInner = (*nicInfoInspectImpl)(nil)

type nicInfoInspectImpl struct {
	name  string
	value ifStateInfo
}

func (impl *nicInfoInspectImpl) ReadData() inspect.Object {
	object := inspect.Object{
		Name: impl.name,
		Properties: []inspect.Property{
			{Key: "Name", Value: inspect.PropertyValueWithStr(impl.value.Name)},
			{Key: "NICID", Value: inspect.PropertyValueWithStr(strconv.FormatUint(uint64(impl.value.nicid), 10))},
			{Key: "AdminUp", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.adminUp))},
			{Key: "LinkOnline", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.linkOnline))},
			{Key: "Up", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.Flags.Up))},
			{Key: "Running", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.Flags.Running))},
			{Key: "Loopback", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.Flags.Loopback))},
			{Key: "Promiscuous", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.Flags.Promiscuous))},
		},
		Metrics: []inspect.Metric{
			{Key: "MTU", Value: inspect.MetricValueWithUintValue(uint64(impl.value.MTU))},
		},
	}
	if linkAddress := impl.value.LinkAddress; len(linkAddress) != 0 {
		object.Properties = append(object.Properties, inspect.Property{
			Key:   "LinkAddress",
			Value: inspect.PropertyValueWithStr(linkAddress.String()),
		})
	}
	for i, protocolAddress := range impl.value.ProtocolAddresses {
		protocol := "unknown"
		switch protocolAddress.Protocol {
		case header.IPv4ProtocolNumber:
			protocol = "ipv4"
		case header.IPv6ProtocolNumber:
			protocol = "ipv6"
		case header.ARPProtocolNumber:
			protocol = "arp"
		}
		object.Properties = append(object.Properties, inspect.Property{
			Key:   fmt.Sprintf("ProtocolAddress%d", i),
			Value: inspect.PropertyValueWithStr(fmt.Sprintf("[%s] %s", protocol, protocolAddress.AddressWithPrefix)),
		})
	}

	for i, addr := range impl.value.dnsServers {
		object.Properties = append(object.Properties, inspect.Property{
			Key: fmt.Sprintf("DNS server%d", i), Value: inspect.PropertyValueWithStr(addr.String())})
	}
	object.Properties = append(object.Properties, inspect.Property{Key: "DHCP enabled", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.dhcpEnabled))})
	return object
}

func (impl *nicInfoInspectImpl) ListChildren() []string {
	children := []string{
		statsLabel,
	}
	if impl.value.dhcpEnabled {
		children = append(children, dhcpInfo)
	}

	switch impl.value.controller.(type) {
	case *eth.Client:
		children = append(children, ethInfo)
	case netdevice.InfoProvider:
		children = append(children, netdeviceInfo)
	}

	return children
}

func (impl *nicInfoInspectImpl) GetChild(childName string) inspectInner {
	switch childName {
	case statsLabel:
		return &statCounterInspectImpl{
			name:  childName,
			value: reflect.ValueOf(impl.value.Stats),
		}
	case dhcpInfo:
		return &dhcpInfoInspectImpl{
			name:  childName,
			info:  impl.value.dhcpInfo,
			stats: impl.value.dhcpStats,
		}
	case ethInfo:
		return &ethInfoInspectImpl{
			name:  childName,
			value: impl.value.controller.(*eth.Client),
		}
	case netdeviceInfo:
		return &netdevInspectImpl{
			name:  childName,
			value: impl.value.controller.(netdevice.InfoProvider),
		}
	default:
		return nil
	}
}

var _ inspectInner = (*dhcpInfoInspectImpl)(nil)

type dhcpInfoInspectImpl struct {
	name  string
	info  dhcp.Info
	stats *dhcp.Stats
}

func (impl *dhcpInfoInspectImpl) ReadData() inspect.Object {
	addrString := func(addr tcpip.Address) string {
		if addr == "" {
			return "[none]"
		}
		return addr.String()
	}
	addrPrefixString := func(addr tcpip.AddressWithPrefix) string {
		if addr == (tcpip.AddressWithPrefix{}) {
			return "[none]"
		}
		return addr.String()
	}
	return inspect.Object{
		Name: impl.name,
		Properties: []inspect.Property{
			{Key: "State", Value: inspect.PropertyValueWithStr(impl.info.State.String())},
			{Key: "AcquiredAddress", Value: inspect.PropertyValueWithStr(addrPrefixString(impl.info.Addr))},
			{Key: "ServerAddress", Value: inspect.PropertyValueWithStr(addrString(impl.info.Server))},
			{Key: "OldAddress", Value: inspect.PropertyValueWithStr(addrPrefixString(impl.info.OldAddr))},
			{Key: "Acquisition", Value: inspect.PropertyValueWithStr(impl.info.Acquisition.String())},
			{Key: "Backoff", Value: inspect.PropertyValueWithStr(impl.info.Backoff.String())},
			{Key: "Retransmission", Value: inspect.PropertyValueWithStr(impl.info.Retransmission.String())},
		},
	}
}

func (impl *dhcpInfoInspectImpl) ListChildren() []string {
	return []string{
		statsLabel,
	}
}

func (impl *dhcpInfoInspectImpl) GetChild(childName string) inspectInner {
	switch childName {
	case statsLabel:
		return &statCounterInspectImpl{
			name:  childName,
			value: reflect.ValueOf(impl.stats).Elem(),
		}
	default:
		return nil
	}
}

var _ inspectInner = (*ethInfoInspectImpl)(nil)

type ethInfoInspectImpl struct {
	name  string
	value *eth.Client
}

func (impl *ethInfoInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
		Metrics: []inspect.Metric{
			{Key: "TxDrops", Value: inspect.MetricValueWithUintValue(impl.value.TxStats().Drops.Value())},
		},
		Properties: []inspect.Property{
			{Key: "Topopath", Value: inspect.PropertyValueWithStr(impl.value.Topopath())},
			{Key: "Filepath", Value: inspect.PropertyValueWithStr(impl.value.Filepath())},
			{Key: "Features", Value: inspect.PropertyValueWithStr(impl.value.Info.Features.String())},
		},
	}
}

func getFifoStatsChildren() []string {
	return []string{
		rxReads,
		rxWrites,
		txReads,
		txWrites,
	}
}

func getFifoStatsImpl(childName string, rx *fifo.RxStats, tx *fifo.TxStats) inspectInner {
	switch childName {
	case rxReads:
		return &fifoStatsInspectImpl{
			name:  childName,
			value: rx.Reads,
			size:  rx.Size(),
		}
	case rxWrites:
		return &fifoStatsInspectImpl{
			name:  childName,
			value: rx.Writes,
			size:  rx.Size(),
		}
	case txReads:
		return &fifoStatsInspectImpl{
			name:  childName,
			value: tx.Reads,
			size:  tx.Size(),
		}
	case txWrites:
		return &fifoStatsInspectImpl{
			name:  childName,
			value: tx.Writes,
			size:  tx.Size(),
		}
	default:
		return nil
	}
}

func (impl *ethInfoInspectImpl) ListChildren() []string {
	return getFifoStatsChildren()
}

func (impl *ethInfoInspectImpl) GetChild(childName string) inspectInner {
	return getFifoStatsImpl(childName, impl.value.RxStats(), impl.value.TxStats())
}

var _ inspectInner = (*netdevInspectImpl)(nil)

type netdevInspectImpl struct {
	name  string
	value netdevice.InfoProvider
}

func (impl *netdevInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
		Metrics: []inspect.Metric{
			{Key: "TxDrops", Value: inspect.MetricValueWithUintValue(impl.value.TxStats().Drops.Value())},
		},
		Properties: []inspect.Property{
			{Key: "Class", Value: inspect.PropertyValueWithStr(impl.value.Info().Class.String())},
		},
	}
}

func (impl *netdevInspectImpl) ListChildren() []string {
	return getFifoStatsChildren()
}

func (impl *netdevInspectImpl) GetChild(childName string) inspectInner {
	return getFifoStatsImpl(childName, impl.value.RxStats(), impl.value.TxStats())
}

var _ inspectInner = (*fifoStatsInspectImpl)(nil)

type fifoStatsInspectImpl struct {
	name  string
	value func(uint32) *tcpip.StatCounter
	size  uint32
}

func (impl *fifoStatsInspectImpl) ReadData() inspect.Object {
	var metrics []inspect.Metric
	for i := uint32(0); i < impl.size; i++ {
		batchSize := i + 1
		if v := impl.value(batchSize).Value(); v != 0 {
			metrics = append(metrics, inspect.Metric{
				Key:   strconv.FormatInt(int64(batchSize), 10),
				Value: inspect.MetricValueWithUintValue(v),
			})
		}
	}
	return inspect.Object{
		Name:    impl.name,
		Metrics: metrics,
	}
}

func (impl *fifoStatsInspectImpl) ListChildren() []string {
	return nil
}

func (impl *fifoStatsInspectImpl) GetChild(string) inspectInner {
	return nil
}

var _ inspectInner = (*socketInfoMapInspectImpl)(nil)

type socketInfoMapInspectImpl struct {
	value *endpointsMap
}

func (impl *socketInfoMapInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: socketInfo,
	}
}

func (impl *socketInfoMapInspectImpl) ListChildren() []string {
	var children []string
	impl.value.Range(func(handle zx.Handle, value tcpip.Endpoint) bool {
		children = append(children, strconv.FormatUint(uint64(handle), 10))
		return true
	})
	return children
}

func (impl *socketInfoMapInspectImpl) GetChild(childName string) inspectInner {
	id, err := strconv.ParseUint(childName, 10, 64)
	if err != nil {
		syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName, "GetChild: %s", err)
		return nil
	}
	if ep, ok := impl.value.Load(zx.Handle(id)); ok {
		return &socketInfoInspectImpl{
			name:  childName,
			info:  ep.Info(),
			state: ep.State(),
			stats: ep.Stats(),
		}
	}
	return nil
}

var _ inspectInner = (*socketInfoInspectImpl)(nil)

type socketInfoInspectImpl struct {
	name  string
	info  tcpip.EndpointInfo
	state uint32
	stats tcpip.EndpointStats
}

func (impl *socketInfoInspectImpl) ReadData() inspect.Object {
	var common stack.TransportEndpointInfo
	var hardError *tcpip.Error
	switch t := impl.info.(type) {
	case *tcp.EndpointInfo:
		common = t.TransportEndpointInfo
		hardError = t.HardError
	case *stack.TransportEndpointInfo:
		common = *t
	default:
		return inspect.Object{
			Name: impl.name,
		}
	}

	var netString string
	switch common.NetProto {
	case header.IPv4ProtocolNumber:
		netString = "IPv4"
	case header.IPv6ProtocolNumber:
		netString = "IPv6"
	default:
		netString = "UNKNOWN"
	}

	var transString string
	var state string
	switch common.TransProto {
	case header.TCPProtocolNumber:
		transString = "TCP"
		state = tcp.EndpointState(impl.state).String()
	case header.UDPProtocolNumber:
		transString = "UDP"
		state = udp.EndpointState(impl.state).String()
	case header.ICMPv4ProtocolNumber:
		transString = "ICMPv4"
	case header.ICMPv6ProtocolNumber:
		transString = "ICMPv6"
	default:
		transString = "UNKNOWN"
	}

	localAddr := fmt.Sprintf("%s:%d", common.ID.LocalAddress.String(), common.ID.LocalPort)
	remoteAddr := fmt.Sprintf("%s:%d", common.ID.RemoteAddress.String(), common.ID.RemotePort)
	properties := []inspect.Property{
		{Key: "NetworkProtocol", Value: inspect.PropertyValueWithStr(netString)},
		{Key: "TransportProtocol", Value: inspect.PropertyValueWithStr(transString)},
		{Key: "State", Value: inspect.PropertyValueWithStr(state)},
		{Key: "LocalAddress", Value: inspect.PropertyValueWithStr(localAddr)},
		{Key: "RemoteAddress", Value: inspect.PropertyValueWithStr(remoteAddr)},
		{Key: "BindAddress", Value: inspect.PropertyValueWithStr(common.BindAddr.String())},
		{Key: "BindNICID", Value: inspect.PropertyValueWithStr(strconv.FormatInt(int64(common.BindNICID), 10))},
		{Key: "RegisterNICID", Value: inspect.PropertyValueWithStr(strconv.FormatInt(int64(common.RegisterNICID), 10))},
	}

	if hardError != nil {
		properties = append(properties, inspect.Property{Key: "HardError", Value: inspect.PropertyValueWithStr(hardError.String())})
	}

	return inspect.Object{
		Name:       impl.name,
		Properties: properties,
	}
}

func (impl *socketInfoInspectImpl) ListChildren() []string {
	return []string{
		statsLabel,
	}
}

func (impl *socketInfoInspectImpl) GetChild(childName string) inspectInner {
	switch childName {
	case statsLabel:
		var value reflect.Value
		switch t := impl.stats.(type) {
		case *tcp.Stats:
			value = reflect.ValueOf(t).Elem()
		case *tcpip.TransportEndpointStats:
			value = reflect.ValueOf(t).Elem()
		default:
			return nil
		}
		return &statCounterInspectImpl{
			name:  childName,
			value: value,
		}
	}
	return nil
}
