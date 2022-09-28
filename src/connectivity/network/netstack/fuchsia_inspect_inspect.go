// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"fmt"
	"net"
	"reflect"
	"sort"
	"strconv"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/netdevice"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	inspect "fidl/fuchsia/inspect/deprecated"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
)

// An infallible version of fuchsia.inspect.Inspect with FIDL details omitted.
type inspectInner interface {
	ReadData() inspect.Object
	ListChildren() []string
	GetChild(string) inspectInner
}

const (
	statsLabel                  = "Stats"
	networkEndpointStatsLabel   = "Network Endpoint Stats"
	socketInfo                  = "Socket Info"
	dhcpInfo                    = "DHCP Info"
	dhcpStateRecentHistoryLabel = "DHCP State Recent History"
	neighborsLabel              = "Neighbors"
	ethInfo                     = "Ethernet Info"
	netdeviceInfo               = "Network Device Info"
	rxReads                     = "RxReads"
	rxWrites                    = "RxWrites"
	txReads                     = "TxReads"
	txWrites                    = "TxWrites"
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
		// The child's lifetime is not tied to the parent's.
		ctx := context.Background()
		return true, svc.AddFn(ctx, childChannel.Channel)
	}
	_ = childChannel.Close()
	return false, nil
}

func (impl *inspectImpl) asService() *component.Service {
	stub := inspect.InspectWithCtxStub{Impl: impl}
	return &component.Service{
		AddFn: func(ctx context.Context, c zx.Channel) error {
			go component.Serve(ctx, &stub, c, component.ServeOptions{
				OnError: func(err error) {
					_ = syslog.WarnTf(inspect.InspectName, "%s", err)
				},
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

func (dir *inspectDirectory) ForEach(fn func(string, component.Node) error) error {
	return fn(inspect.InspectName, dir.asService())
}

// statCounter enables *tcpip.StatCounters and other types in this
// package to be accessed via the same interface.
type statCounter interface {
	Value(...string) uint64
}

var _ statCounter = (*tcpip.StatCounter)(nil)
var statCounterType = reflect.TypeOf((*statCounter)(nil)).Elem()

// Recursive reflection-based implementation for structs containing other
// structs, stat counters, or maps of stat counters.

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
		field := typ.Field(i)
		// PkgPath is empty for exported field names.
		if len(field.PkgPath) != 0 {
			continue
		}

		if _, ok := extractIntegralStatCounterMap(impl.value.Field(i)); ok {
			children = append(children, field.Name)
		} else if field.Type.Kind() == reflect.Struct && !field.Type.Implements(statCounterType) && !reflect.PtrTo(field.Type).Implements(statCounterType) {
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
		// PkgPath is empty for exported field names.
		if len(typ.PkgPath) != 0 {
			return nil
		}
		if child := impl.value.FieldByName(childName); child.IsValid() {
			if counterMap, ok := extractIntegralStatCounterMap(child); ok {
				return &integralStatCounterMapInspectImpl{
					name:  childName,
					value: counterMap,
				}
			}
			if typ.Type.Kind() == reflect.Struct {
				return &statCounterInspectImpl{
					name:  childName,
					value: child,
				}
			}
		}
	}
	return nil
}

func extractIntegralStatCounterMap(value reflect.Value) (*tcpip.IntegralStatCounterMap, bool) {
	switch t := value.Interface().(type) {
	case *tcpip.IntegralStatCounterMap:
		return t, true
	case tcpip.IntegralStatCounterMap:
		return &t, true
	default:
		return nil, false
	}
}

var _ inspectInner = (*logEntryInspectImpl)(nil)

type logEntryInspectImpl struct {
	index string
	entry util.LogEntry
}

func (impl *logEntryInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.index,
		Properties: []inspect.Property{
			{Key: "@time", Value: inspect.PropertyValueWithStr(strconv.FormatInt(int64(impl.entry.Timestamp), 10))},
			{Key: "value", Value: inspect.PropertyValueWithStr(impl.entry.Content)},
		},
	}
}

func (impl *logEntryInspectImpl) ListChildren() []string {
	return nil
}

func (impl *logEntryInspectImpl) GetChild(childName string) inspectInner {
	return nil
}

var _ inspectInner = (*circularLogsInspectImpl)(nil)

type circularLogsInspectImpl struct {
	name  string
	value []util.LogEntry
}

func (impl *circularLogsInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
	}
}

func (impl *circularLogsInspectImpl) ListChildren() []string {
	children := make([]string, 0, len(impl.value))
	for i := range impl.value {
		children = append(children, strconv.FormatUint(uint64(i), 10))
	}
	return children
}

func (impl *circularLogsInspectImpl) GetChild(childName string) inspectInner {
	index, err := strconv.ParseUint(childName, 10, 64)
	if err != nil {
		_ = syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName, "GetChild(): %s", err)
		return nil
	}
	if index >= uint64(len(impl.value)) {
		_ = syslog.VLogTf(
			syslog.DebugVerbosity,
			inspect.InspectName,
			"GetChild(%s): index %d out of bounds, there are %d entries in the circular logs",
			childName,
			index,
			len(impl.value),
		)
		return nil
	}
	return &logEntryInspectImpl{
		index: childName,
		entry: impl.value[index],
	}
}

var _ inspectInner = (*integralStatCounterMapInspectImpl)(nil)

type integralStatCounterMapInspectImpl struct {
	name  string
	value *tcpip.IntegralStatCounterMap
}

const integralStatMapTotalFieldName = "Total"

func (impl *integralStatCounterMapInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
	}
}

func (impl *integralStatCounterMapInspectImpl) ListChildren() []string {
	var children []string
	children = append(children, integralStatMapTotalFieldName)
	for _, key := range impl.value.Keys() {
		children = append(children, strconv.FormatUint(key, 10))
	}
	return children
}

func (impl *integralStatCounterMapInspectImpl) GetChild(childName string) inspectInner {
	if childName == integralStatMapTotalFieldName {
		var total uint64
		for _, key := range impl.value.Keys() {
			if counter, ok := impl.value.Get(key); ok {
				total += counter.Value()
			}
		}
		return &singleStatCounterInspectImpl{
			name:  childName,
			value: total,
		}
	} else {
		if key, err := strconv.ParseUint(childName, 10, 64); err == nil {
			if counter, ok := impl.value.Get(key); ok {
				return &singleStatCounterInspectImpl{
					name:  childName,
					value: counter.Value(),
				}
			}
		}
		return nil
	}
}

var _ inspectInner = (*singleStatCounterInspectImpl)(nil)

type singleStatCounterInspectImpl struct {
	name  string
	value uint64
}

func (impl *singleStatCounterInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
		Properties: []inspect.Property{
			{Key: "Count", Value: inspect.PropertyValueWithStr(strconv.FormatUint(impl.value, 10))},
		},
	}
}

func (*singleStatCounterInspectImpl) ListChildren() []string {
	return nil
}

func (*singleStatCounterInspectImpl) GetChild(childName string) inspectInner {
	return nil
}

var _ inspectInner = (*fidlStatsInspectImpl)(nil)

type fidlStatsInspectImpl struct {
	name                      string
	fidlInterfaceWatcherStats *fidlInterfaceWatcherStats
}

func (impl *fidlStatsInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
		Metrics: []inspect.Metric{
			{
				Key:   "InterfaceWatcherCount",
				Value: inspect.MetricValueWithIntValue(impl.fidlInterfaceWatcherStats.count.Load()),
			},
		},
	}
}

func (*fidlStatsInspectImpl) ListChildren() []string {
	return nil
}

func (*fidlStatsInspectImpl) GetChild(childName string) inspectInner {
	return nil
}

var _ inspectInner = (*nicInfoMapInspectImpl)(nil)

// Picking info to inspect from ifState in netstack.
type ifStateInfo struct {
	stack.NICInfo
	nicid                  tcpip.NICID
	adminUp, linkOnline    bool
	dnsServers             []tcpip.Address
	dhcpEnabled            bool
	dhcpInfo               dhcp.Info
	dhcpStateRecentHistory []util.LogEntry
	dhcpStats              *dhcp.Stats
	controller             link.Controller
	neighbors              map[string]stack.NeighborEntry
	networkEndpointStats   map[string]stack.NetworkEndpointStats
}

type nicInfoMapInspectImpl struct {
	value map[tcpip.NICID]ifStateInfo
}

func (*nicInfoMapInspectImpl) ReadData() inspect.Object {
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
		_ = syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName, "GetChild(): %s", err)
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
	object.Properties = append(object.Properties, inspect.Property{
		Key:   "DHCP enabled",
		Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.dhcpEnabled)),
	})
	return object
}

func (impl *nicInfoInspectImpl) ListChildren() []string {
	children := []string{
		statsLabel,
	}
	if len(impl.value.NetworkStats) != 0 {
		children = append(children, networkEndpointStatsLabel)
	}
	if impl.value.dhcpEnabled {
		children = append(children, dhcpInfo)
	}
	if impl.value.neighbors != nil {
		children = append(children, neighborsLabel)
	}

	switch impl.value.controller.(type) {
	case *eth.Client:
		children = append(children, ethInfo)
	case *netdevice.Port:
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
	case networkEndpointStatsLabel:
		return &networkEndpointStatsInspectImpl{
			name:  childName,
			value: impl.value.networkEndpointStats,
		}
	case dhcpInfo:
		return &dhcpInfoInspectImpl{
			name:               childName,
			info:               impl.value.dhcpInfo,
			stateRecentHistory: impl.value.dhcpStateRecentHistory,
			stats:              impl.value.dhcpStats,
		}
	case neighborsLabel:
		return &neighborTableInspectImpl{
			name:  childName,
			value: impl.value.neighbors,
		}
	case ethInfo:
		return &ethInfoInspectImpl{
			name:  childName,
			value: impl.value.controller.(*eth.Client),
		}
	case netdeviceInfo:
		return &netdevInspectImpl{
			name:  childName,
			value: impl.value.controller.(*netdevice.Port),
		}
	default:
		return nil
	}
}

var _ inspectInner = (*networkEndpointStatsInspectImpl)(nil)

type networkEndpointStatsInspectImpl struct {
	name  string
	value map[string]stack.NetworkEndpointStats
}

func (impl *networkEndpointStatsInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
	}
}

func (impl *networkEndpointStatsInspectImpl) ListChildren() []string {
	children := make([]string, 0, len(impl.value))
	for k := range impl.value {
		children = append(children, k)
	}
	return children
}

func (impl *networkEndpointStatsInspectImpl) GetChild(childName string) inspectInner {
	entry, ok := impl.value[childName]
	if !ok {
		_ = syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName,
			"GetChild(%s): no stats found",
			childName,
		)
		return nil
	}

	return &statCounterInspectImpl{
		name:  childName,
		value: reflect.Indirect(reflect.ValueOf(entry)),
	}
}

var _ inspectInner = (*dhcpInfoInspectImpl)(nil)

type dhcpInfoInspectImpl struct {
	name               string
	info               dhcp.Info
	stateRecentHistory []util.LogEntry
	stats              *dhcp.Stats
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
	maskString := func(mask tcpip.AddressMask) string {
		if mask.String() == "" {
			return "[none]"
		}
		return mask.String()
	}
	properties := []inspect.Property{
		{Key: "State", Value: inspect.PropertyValueWithStr(impl.info.State.String())},
		{Key: "AcquiredAddress", Value: inspect.PropertyValueWithStr(addrPrefixString(impl.info.Acquired))},
		{Key: "AssignedAddress", Value: inspect.PropertyValueWithStr(addrPrefixString(impl.info.Assigned))},
		{Key: "Acquisition", Value: inspect.PropertyValueWithStr(impl.info.Acquisition.String())},
		{Key: "Backoff", Value: inspect.PropertyValueWithStr(impl.info.Backoff.String())},
		{Key: "Retransmission", Value: inspect.PropertyValueWithStr(impl.info.Retransmission.String())},
		{Key: "LeaseExpiration", Value: inspect.PropertyValueWithStr(impl.info.LeaseExpiration.String())},
		{Key: "RenewTime", Value: inspect.PropertyValueWithStr(impl.info.RenewTime.String())},
		{Key: "RebindTime", Value: inspect.PropertyValueWithStr(impl.info.RebindTime.String())},
		{Key: "Config.ServerAddress", Value: inspect.PropertyValueWithStr(addrString(impl.info.Config.ServerAddress))},
		{Key: "Config.SubnetMask", Value: inspect.PropertyValueWithStr(maskString(impl.info.Config.SubnetMask))},
	}
	for i, router := range impl.info.Config.Router {
		properties = append(properties, inspect.Property{
			Key:   fmt.Sprintf("Config.Router%d", i),
			Value: inspect.PropertyValueWithStr(addrString(router)),
		})
	}
	for i, dns := range impl.info.Config.DNS {
		properties = append(properties, inspect.Property{
			Key:   fmt.Sprintf("Config.DNS%d", i),
			Value: inspect.PropertyValueWithStr(addrString(dns)),
		})
	}
	properties = append(properties, []inspect.Property{
		{Key: "Config.LeaseLength", Value: inspect.PropertyValueWithStr(impl.info.Config.LeaseLength.String())},
		{Key: "Config.RenewTime", Value: inspect.PropertyValueWithStr(impl.info.Config.RenewTime.String())},
		{Key: "Config.RebindTime", Value: inspect.PropertyValueWithStr(impl.info.Config.RebindTime.String())},
		{Key: "Config.Declined", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.info.Config.Declined))},
	}...)
	return inspect.Object{
		Name:       impl.name,
		Properties: properties,
	}
}

func (*dhcpInfoInspectImpl) ListChildren() []string {
	return []string{
		statsLabel,
		dhcpStateRecentHistoryLabel,
	}
}

func (impl *dhcpInfoInspectImpl) GetChild(childName string) inspectInner {
	switch childName {
	case statsLabel:
		return &statCounterInspectImpl{
			name:  childName,
			value: reflect.ValueOf(impl.stats).Elem(),
		}
	case dhcpStateRecentHistoryLabel:
		return &circularLogsInspectImpl{
			name:  childName,
			value: impl.stateRecentHistory,
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

func (*ethInfoInspectImpl) ListChildren() []string {
	return getFifoStatsChildren()
}

func (impl *ethInfoInspectImpl) GetChild(childName string) inspectInner {
	return getFifoStatsImpl(childName, impl.value.RxStats(), impl.value.TxStats())
}

var _ inspectInner = (*netdevInspectImpl)(nil)

type netdevInspectImpl struct {
	name  string
	value *netdevice.Port
}

func (impl *netdevInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
		Metrics: []inspect.Metric{
			{Key: "TxDrops", Value: inspect.MetricValueWithUintValue(impl.value.TxStats().Drops.Value())},
		},
		Properties: []inspect.Property{
			{Key: "Class", Value: inspect.PropertyValueWithStr(impl.value.Class().String())},
		},
	}
}

func (*netdevInspectImpl) ListChildren() []string {
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

// We cap the number of Metrics in a response so that the FIDL
// response would fit in a single channel message.
const maxMetricsForFifoStats = 1024

func (impl *fifoStatsInspectImpl) ReadData() inspect.Object {
	var metrics []inspect.Metric
	batchesPerMetrics := ((impl.size - 1) / maxMetricsForFifoStats) + 1
	batch := uint32(1)
	for batch <= impl.size {
		startBatch := batch
		v := uint64(0)
		for i := uint32(0); i < batchesPerMetrics; i++ {
			v += impl.value(batch).Value()
			batch++
			if batch > impl.size {
				break
			}
		}
		endBatch := batch - 1
		if v != 0 {
			var key string
			if startBatch == endBatch {
				key = fmt.Sprintf("%d", startBatch)
			} else {
				key = fmt.Sprintf("%d-%d", startBatch, endBatch)
			}
			metrics = append(metrics, inspect.Metric{
				Key:   key,
				Value: inspect.MetricValueWithUintValue(v),
			})
		}
	}
	return inspect.Object{
		Name:    impl.name,
		Metrics: metrics,
	}
}

func (*fifoStatsInspectImpl) ListChildren() []string {
	return nil
}

func (*fifoStatsInspectImpl) GetChild(string) inspectInner {
	return nil
}

var _ inspectInner = (*socketInfoMapInspectImpl)(nil)

type socketInfoMapInspectImpl struct {
	value *endpointsMap
}

func (*socketInfoMapInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: socketInfo,
	}
}

func (impl *socketInfoMapInspectImpl) ListChildren() []string {
	var children []string
	impl.value.Range(func(key uint64, _ tcpip.Endpoint) bool {
		children = append(children, strconv.FormatUint(uint64(key), 10))
		return true
	})
	return children
}

func (impl *socketInfoMapInspectImpl) GetChild(childName string) inspectInner {
	id, err := strconv.ParseUint(childName, 10, 64)
	if err != nil {
		_ = syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName, "GetChild(): %s", err)
		return nil
	}
	if ep, ok := impl.value.Load(uint64(id)); ok {
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
	switch t := impl.info.(type) {
	case *stack.TransportEndpointInfo:
		common = *t
	default:
		return inspect.Object{
			Name: impl.name,
		}
	}

	var netString string
	var zeroAddress net.IP
	switch common.NetProto {
	case header.IPv4ProtocolNumber:
		netString = "IPv4"
		zeroAddress = net.IPv4zero
	case header.IPv6ProtocolNumber:
		netString = "IPv6"
		zeroAddress = net.IPv6zero
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
		state = transport.DatagramEndpointState(impl.state).String()
	case header.ICMPv4ProtocolNumber:
		transString = "ICMPv4"
	case header.ICMPv6ProtocolNumber:
		transString = "ICMPv6"
	default:
		transString = "UNKNOWN"
	}

	localAddress := net.IP(common.ID.LocalAddress)
	if len(localAddress) == 0 {
		localAddress = zeroAddress
	}
	remoteAddress := net.IP(common.ID.RemoteAddress)
	if len(remoteAddress) == 0 {
		remoteAddress = zeroAddress
	}

	localAddr := net.JoinHostPort(localAddress.String(), strconv.FormatUint(uint64(common.ID.LocalPort), 10))
	remoteAddr := net.JoinHostPort(remoteAddress.String(), strconv.FormatUint(uint64(common.ID.RemotePort), 10))
	properties := []inspect.Property{
		{Key: "NetworkProtocol", Value: inspect.PropertyValueWithStr(netString)},
		{Key: "TransportProtocol", Value: inspect.PropertyValueWithStr(transString)},
		{Key: "State", Value: inspect.PropertyValueWithStr(state)},
		{Key: "LocalAddress", Value: inspect.PropertyValueWithStr(localAddr)},
		{Key: "RemoteAddress", Value: inspect.PropertyValueWithStr(remoteAddr)},
		{Key: "BindAddress", Value: inspect.PropertyValueWithStr(common.BindAddr.String())},
		{Key: "BindNICID", Value: inspect.PropertyValueWithStr(strconv.FormatUint(uint64(common.BindNICID), 10))},
		{Key: "RegisterNICID", Value: inspect.PropertyValueWithStr(strconv.FormatUint(uint64(common.RegisterNICID), 10))},
	}

	return inspect.Object{
		Name:       impl.name,
		Properties: properties,
	}
}

func (*socketInfoInspectImpl) ListChildren() []string {
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

var _ inspectInner = (*routingTableInspectImpl)(nil)

type routingTableInspectImpl struct {
	value []routes.ExtendedRoute
}

func (*routingTableInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: "Routes",
	}
}

func (impl *routingTableInspectImpl) ListChildren() []string {
	children := make([]string, len(impl.value))
	for i := range impl.value {
		children[i] = strconv.FormatUint(uint64(i), 10)
	}
	return children
}

func (impl *routingTableInspectImpl) GetChild(childName string) inspectInner {
	routeIndex, err := strconv.ParseUint(childName, 10, 64)
	if err != nil {
		_ = syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName, "GetChild(): %s", err)
		return nil
	}
	if routeIndex >= uint64(len(impl.value)) {
		_ = syslog.VLogTf(
			syslog.DebugVerbosity,
			inspect.InspectName,
			"GetChild(%s): index %d out of bounds, there are %d entries in the routing table",
			childName,
			routeIndex,
			len(impl.value),
		)
		return nil
	}
	return &routeInfoInspectImpl{
		name:  childName,
		value: impl.value[routeIndex],
	}
}

var _ inspectInner = (*routeInfoInspectImpl)(nil)

type routeInfoInspectImpl struct {
	name  string
	value routes.ExtendedRoute
}

func (impl *routeInfoInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
		Properties: []inspect.Property{
			{Key: "Destination", Value: inspect.PropertyValueWithStr(impl.value.Route.Destination.String())},
			{Key: "Gateway", Value: inspect.PropertyValueWithStr(impl.value.Route.Gateway.String())},
			{Key: "NIC", Value: inspect.PropertyValueWithStr(strconv.FormatUint(uint64(impl.value.Route.NIC), 10))},
			{Key: "Metric", Value: inspect.PropertyValueWithStr(strconv.FormatUint(uint64(impl.value.Metric), 10))},
			{Key: "MetricTracksInterface", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.MetricTracksInterface))},
			{Key: "Dynamic", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.Dynamic))},
			{Key: "Enabled", Value: inspect.PropertyValueWithStr(strconv.FormatBool(impl.value.Enabled))},
		},
	}
}

func (*routeInfoInspectImpl) ListChildren() []string {
	return nil
}

func (*routeInfoInspectImpl) GetChild(string) inspectInner {
	return nil
}

var _ inspectInner = (*neighborTableInspectImpl)(nil)

type neighborTableInspectImpl struct {
	name  string
	value map[string]stack.NeighborEntry
}

func (impl *neighborTableInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.name,
	}
}

func (impl *neighborTableInspectImpl) ListChildren() []string {
	children := make([]string, 0, len(impl.value))
	for k := range impl.value {
		children = append(children, k)
	}
	return children
}

func (impl *neighborTableInspectImpl) GetChild(childName string) inspectInner {
	entry, ok := impl.value[childName]
	if !ok {
		_ = syslog.VLogTf(syslog.DebugVerbosity, inspect.InspectName,
			"GetChild(%s): no entry found in the neighbor table",
			childName,
		)
		return nil
	}
	return &neighborInfoInspectImpl{
		value: entry,
	}
}

var _ inspectInner = (*neighborInfoInspectImpl)(nil)

type neighborInfoInspectImpl struct {
	value stack.NeighborEntry
}

func (impl *neighborInfoInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: impl.value.Addr.String(),
		Properties: []inspect.Property{
			{Key: "Link address", Value: inspect.PropertyValueWithStr(impl.value.LinkAddr.String())},
			{Key: "State", Value: inspect.PropertyValueWithStr(impl.value.State.String())},
		},
		Metrics: []inspect.Metric{
			{Key: "Last updated", Value: inspect.MetricValueWithIntValue(int64(fidlconv.ToZxTime(impl.value.UpdatedAt)))},
		},
	}
}

func (*neighborInfoInspectImpl) ListChildren() []string {
	return nil
}

func (*neighborInfoInspectImpl) GetChild(string) inspectInner {
	return nil
}
