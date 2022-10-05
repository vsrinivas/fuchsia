// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"fmt"
	"reflect"
	"sort"
	"strconv"
	"syscall/zx"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	ethernetext "go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlext/fuchsia/hardware/ethernet"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"fidl/fuchsia/hardware/ethernet"
	inspect "fidl/fuchsia/inspect/deprecated"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.uber.org/multierr"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

const (
	ipv4Addr = tcpip.Address("\xc0\xa8\x01\x01")
	ipv6Addr = tcpip.Address("\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01")
)

type inspectNodeExpectation struct {
	node     inspect.Object
	children []inspectNodeExpectation
}

func checkInspectRecurse(node inspectInner, expected inspectNodeExpectation) error {
	var err error

	nodeData := node.ReadData()

	if nodeData.Name != expected.node.Name {
		err = multierr.Append(err, fmt.Errorf("found unexpected name %s instead of %s", nodeData.Name, expected.node.Name))
	}

	if diff := cmp.Diff(expected.node.Properties, nodeData.Properties, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{})); diff != "" {
		err = multierr.Append(err, fmt.Errorf("Properties mismatch (-want +got):\n%s", diff))
	}

	containsMetric := func(metrics []inspect.Metric, metricToFind inspect.Metric) bool {
		for _, metric := range metrics {
			if metric == metricToFind {
				return true
			}
		}

		return false
	}

	for _, metric := range nodeData.Metrics {
		if metric.Value != inspect.MetricValueWithUintValue(0) && !containsMetric(expected.node.Metrics, metric) {
			err = multierr.Append(err, fmt.Errorf("ReadData() mismatch: found unexpected non-zero metric %#v", metric))
		}
	}

	for _, metric := range expected.node.Metrics {
		if !containsMetric(nodeData.Metrics, metric) {
			err = multierr.Append(err, fmt.Errorf("ReadData() mismatch: missing expected non-zero metric %#v", metric))
		}
	}

	children := node.ListChildren()

	var expectedChildrenNames []string
	for _, child := range expected.children {
		expectedChildrenNames = append(expectedChildrenNames, child.node.Name)
	}

	if diff := cmp.Diff(expectedChildrenNames, children, cmpopts.SortSlices(func(a, b string) bool {
		return a < b
	})); diff != "" {
		err = multierr.Append(err, fmt.Errorf("ListChildren() mismatch (-want +got):\n%s", diff))
	}

	childName := "not a real child"
	if child := node.GetChild(childName); child != nil {
		err = multierr.Append(err, fmt.Errorf("got GetChild(%s) = %s, want = nil", childName, child))
	}

	for i, childName := range expectedChildrenNames {
		if child := node.GetChild(childName); child != nil {
			err = multierr.Append(err, checkInspectRecurse(child, expected.children[i]))
		} else {
			err = multierr.Append(err, fmt.Errorf("got GetChild(%s) = nil, want non-nil", childName))
		}
	}
	return err
}

func TestStatCounterInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	const invalidPort = 1
	const invalidPortCount = 10
	const initAcquireCount = 3
	var s dhcp.Stats
	s.PacketDiscardStats.InvalidPort.Init()
	s.PacketDiscardStats.InvalidPacketType.Init()
	s.PacketDiscardStats.InvalidTransProto.Init()
	for i := 0; i < invalidPortCount; i++ {
		s.PacketDiscardStats.InvalidPort.Increment(invalidPort)
	}

	s.InitAcquire.IncrementBy(initAcquireCount)

	v := statCounterInspectImpl{
		name:  "doesn't matter",
		value: reflect.ValueOf(&s).Elem(),
	}

	expected := inspectNodeExpectation{
		node: inspect.Object{
			Name: "doesn't matter",
			Metrics: []inspect.Metric{
				{Key: "InitAcquire", Value: inspect.MetricValueWithUintValue(initAcquireCount)},
			},
		},
		children: []inspectNodeExpectation{
			{
				node: inspect.Object{
					Name: "PacketDiscardStats",
				},
				children: []inspectNodeExpectation{
					{
						node: inspect.Object{
							Name: "InvalidPort",
						},
						children: []inspectNodeExpectation{
							{
								node: inspect.Object{
									Name: "1",
									Properties: []inspect.Property{
										{Key: "Count", Value: inspect.PropertyValueWithStr("10")},
									},
								},
							},
							{
								node: inspect.Object{
									Name: "Total",
									Properties: []inspect.Property{
										{Key: "Count", Value: inspect.PropertyValueWithStr("10")},
									},
								},
							},
						},
					},
					{
						node: inspect.Object{
							Name: "InvalidTransProto",
						},
						children: []inspectNodeExpectation{
							{
								node: inspect.Object{
									Name: "Total",
									Properties: []inspect.Property{
										{Key: "Count", Value: inspect.PropertyValueWithStr("0")},
									},
								},
							},
						},
					},
					{
						node: inspect.Object{
							Name: "InvalidPacketType",
						},
						children: []inspectNodeExpectation{
							{
								node: inspect.Object{
									Name: "Total",
									Properties: []inspect.Property{
										{Key: "Count", Value: inspect.PropertyValueWithStr("0")},
									},
								},
							},
						},
					},
				},
			},
		},
	}

	if err := checkInspectRecurse(&v, expected); err != nil {
		t.Error(err)
	}
}

func circularLogsChecker(logs *circularLogsInspectImpl, expectedChildren []string, expectedChildrenData []inspect.Object) []error {
	var errors []error

	if diff := cmp.Diff(inspect.Object{
		Name: logs.name,
	}, logs.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{})); diff != "" {
		errors = append(errors, fmt.Errorf("ReadData() mismatch (-want +got):\n%s", diff))
	}

	children := logs.ListChildren()
	if diff := cmp.Diff(expectedChildren, children); diff != "" {
		errors = append(errors, fmt.Errorf("ListChildren() mismatch (-want +got):\n%s", diff))
		return errors
	}

	childName := "not a real child"
	if child := logs.GetChild(childName); child != nil {
		errors = append(errors, fmt.Errorf("got GetChild(%s) = %s, want = nil", childName, child))
	}

	for i, childName := range children {
		if child := logs.GetChild(childName); child == nil {
			errors = append(errors, fmt.Errorf("got GetChild(%s) = nil, want non-nil", childName))
		} else if entry, ok := child.(*logEntryInspectImpl); !ok {
			errors = append(errors, fmt.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*statCounterInspectImpl)(nil)))
		} else {
			if diff := cmp.Diff(expectedChildrenData[i], entry.ReadData(), cmpopts.IgnoreUnexported(
				inspect.Object{}, inspect.Metric{})); diff != "" {
				errors = append(errors, fmt.Errorf("ReadData() mismatch (-want +got):\n%s", diff))
			}
			if diff := cmp.Diff(child.ListChildren(), []string(nil)); diff != "" {
				errors = append(errors, fmt.Errorf("ListChildren() mismatch (-want +got):\n%s", diff))
			}
		}
	}

	return errors
}

func TestCircularLogsInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	v := circularLogsInspectImpl{
		name: "dosn't matter",
		value: []util.LogEntry{
			{Timestamp: 42, Content: "foo"},
			{Timestamp: 1337, Content: "bar"},
		},
	}

	expectedChildren := []string{"0", "1"}
	expectedChildrenData := []inspect.Object{
		{
			Name: "0",
			Properties: []inspect.Property{
				{Key: "@time", Value: inspect.PropertyValueWithStr("42")},
				{Key: "value", Value: inspect.PropertyValueWithStr("foo")},
			},
		},
		{
			Name: "1",
			Properties: []inspect.Property{
				{Key: "@time", Value: inspect.PropertyValueWithStr("1337")},
				{Key: "value", Value: inspect.PropertyValueWithStr("bar")},
			},
		},
	}

	errors := circularLogsChecker(&v, expectedChildren, expectedChildrenData)
	for _, err := range errors {
		t.Error(err)
	}
}

func TestIntegralStatCounterMapInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	var integralStatCounterMap tcpip.IntegralStatCounterMap
	integralStatCounterMap.Init()
	const firstKey = 1
	const firstValue = 10
	for i := 0; i < firstValue; i++ {
		integralStatCounterMap.Increment(firstKey)
	}

	const secondKey = 2
	const secondValue = 20
	for i := 0; i < secondValue; i++ {
		integralStatCounterMap.Increment(secondKey)
	}
	v := integralStatCounterMapInspectImpl{
		name:  "doesn't matter",
		value: &integralStatCounterMap,
	}

	expected := inspectNodeExpectation{
		node: inspect.Object{
			Name: "doesn't matter",
		},
		children: []inspectNodeExpectation{
			{
				node: inspect.Object{
					Name: strconv.Itoa(firstKey),
					Properties: []inspect.Property{
						{Key: "Count", Value: inspect.PropertyValueWithStr("10")},
					},
				},
			},
			{
				node: inspect.Object{
					Name: strconv.Itoa(secondKey),
					Properties: []inspect.Property{
						{Key: "Count", Value: inspect.PropertyValueWithStr("20")},
					},
				},
			},
			{
				node: inspect.Object{
					Name: "Total",
					Properties: []inspect.Property{
						{Key: "Count", Value: inspect.PropertyValueWithStr("30")},
					},
				},
			},
		},
	}

	if err := checkInspectRecurse(&v, expected); err != nil {
		t.Error(err)
	}
}

func TestSocketStatCounterInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	// Create a new netstack and add TCP and UDP endpoints.
	ns, _ := newNetstack(t, netstackTestOptions{})
	wq := new(waiter.Queue)
	tcpEP, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		t.Fatal(err)
	}
	udpEP, err := ns.stack.NewEndpoint(udp.ProtocolNumber, ipv6.ProtocolNumber, wq)
	if err != nil {
		t.Fatal(err)
	}
	v := socketInfoMapInspectImpl{
		value: &ns.endpoints,
	}
	children := v.ListChildren()
	if diff := cmp.Diff([]string(nil), children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}

	// Add the 2 endpoints to endpoints with key being the transport protocol
	// number.
	const key1 = 1
	const key2 = 2
	ns.endpoints.Store(key1, tcpEP)
	ns.endpoints.Store(key2, udpEP)

	children = v.ListChildren()
	sort.Strings(children)
	if diff := cmp.Diff([]string{strconv.Itoa(key1), strconv.Itoa(key2)}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	for _, name := range children {
		child := v.GetChild(name)
		if child == nil {
			t.Fatalf("got GetChild(%s) = nil, want non-nil", name)
		}

		val, err := strconv.ParseUint(name, 10, 64)
		if err != nil {
			t.Fatalf("string parsing error %s", err)
		}

		// Update protocol specific expected values.
		var (
			networkProtoName, transportProtoName string
			unspecifiedAddress                   string
			expectInspectObj                     inspect.Object
		)
		switch val {
		case key1:
			networkProtoName = "IPv4"
			transportProtoName = "TCP"
			unspecifiedAddress = "0.0.0.0"
			expectInspectObj = inspect.Object{
				Name: "Stats",
				Metrics: []inspect.Metric{
					{Key: "SegmentsReceived", Value: inspect.MetricValueWithUintValue(0)},
					{Key: "SegmentsSent", Value: inspect.MetricValueWithUintValue(0)},
					{Key: "FailedConnectionAttempts", Value: inspect.MetricValueWithUintValue(0)},
				},
			}
		case key2:
			networkProtoName = "IPv6"
			transportProtoName = "UDP"
			unspecifiedAddress = "[::]"
			expectInspectObj = inspect.Object{
				Name: "Stats",
				Metrics: []inspect.Metric{
					{Key: "PacketsReceived", Value: inspect.MetricValueWithUintValue(0)},
					{Key: "PacketsSent", Value: inspect.MetricValueWithUintValue(0)},
				},
			}
		}

		epChildren := child.ListChildren()
		if diff := cmp.Diff([]string{
			"Stats",
		}, epChildren); diff != "" {
			t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
		}
		for _, c := range epChildren {
			if c == "Stats" {
				statsChild := child.GetChild(c)
				if diff := cmp.Diff([]string{
					"ReceiveErrors",
					"ReadErrors",
					"SendErrors",
					"WriteErrors",
				}, statsChild.ListChildren()); diff != "" {
					t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
				}

				// Compare against the expected inspect objects.
				if diff := cmp.Diff(expectInspectObj, statsChild.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{}, inspect.Property{})); diff != "" {
					t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
				}
			}
		}

		if diff := cmp.Diff(inspect.Object{
			Name: name,
			Properties: []inspect.Property{
				{Key: "NetworkProtocol", Value: inspect.PropertyValueWithStr(networkProtoName)},
				{Key: "TransportProtocol", Value: inspect.PropertyValueWithStr(transportProtoName)},
				{Key: "State", Value: inspect.PropertyValueWithStr("INITIAL")},
				{Key: "LocalAddress", Value: inspect.PropertyValueWithStr(unspecifiedAddress + ":0")},
				{Key: "RemoteAddress", Value: inspect.PropertyValueWithStr(unspecifiedAddress + ":0")},
				{Key: "BindAddress", Value: inspect.PropertyValueWithStr("")},
				{Key: "BindNICID", Value: inspect.PropertyValueWithStr("0")},
				{Key: "RegisterNICID", Value: inspect.PropertyValueWithStr("0")},
			},
		}, child.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{}, inspect.Property{})); diff != "" {
			t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
		}
	}

	// Empty the endpoints.
	ns.endpoints.Range(func(key uint64, _ tcpip.Endpoint) bool {
		ns.endpoints.Delete(key)
		return true
	})
	children = v.ListChildren()
	if diff := cmp.Diff([]string(nil), children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
}

func TestNicInfoMapInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	v := nicInfoMapInspectImpl{
		value: map[tcpip.NICID]ifStateInfo{
			1: {},
			2: {},
		},
	}
	children := v.ListChildren()
	if diff := cmp.Diff([]string{
		"1", "2",
	}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*nicInfoInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*nicInfoInspectImpl)(nil))
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	if diff := cmp.Diff(inspect.Object{
		Name: "NICs",
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestNicInfoInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	v := nicInfoInspectImpl{
		name: "doesn't matter",
	}
	children := v.ListChildren()
	if diff := cmp.Diff([]string{
		"Stats",
	}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*statCounterInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*statCounterInspectImpl)(nil))
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	v.value.nicid = 5
	v.value.Flags.Up = true
	v.value.Flags.Loopback = true
	v.value.dnsServers = []tcpip.Address{ipv4Addr}

	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Properties: []inspect.Property{
			{Key: "Name", Value: inspect.PropertyValueWithStr(v.value.Name)},
			{Key: "NICID", Value: inspect.PropertyValueWithStr("5")},
			{Key: "AdminUp", Value: inspect.PropertyValueWithStr("false")},
			{Key: "LinkOnline", Value: inspect.PropertyValueWithStr("false")},
			{Key: "Up", Value: inspect.PropertyValueWithStr("true")},
			{Key: "Running", Value: inspect.PropertyValueWithStr("false")},
			{Key: "Loopback", Value: inspect.PropertyValueWithStr("true")},
			{Key: "Promiscuous", Value: inspect.PropertyValueWithStr("false")},
			{Key: "DNS server0", Value: inspect.PropertyValueWithStr(ipv4Addr.String())},
			{Key: "DHCP enabled", Value: inspect.PropertyValueWithStr("false")},
		},
		Metrics: []inspect.Metric{
			{Key: "MTU", Value: inspect.MetricValueWithUintValue(uint64(v.value.MTU))},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestDHCPInfoInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	var invalidPortCounter, invalidTransProtoCounter, invalidPacketTypeCounter, invalidPacketType2Counter tcpip.StatCounter

	const invalidPort = 1
	const invalidPortCount = 10
	invalidPortCounter.IncrementBy(invalidPortCount)

	const invalidTransProto = 2
	const invalidTransProtoCount = 20
	invalidTransProtoCounter.IncrementBy(invalidTransProtoCount)

	const invalidPacketType = 3
	const invalidPacketTypeCount = 30
	invalidPacketTypeCounter.IncrementBy(invalidPacketTypeCount)

	const invalidPacketType2 = 4
	const invalidPacketType2Count = 40
	invalidPacketType2Counter.IncrementBy(invalidPacketType2Count)

	const counterPropertyKey = "Count"

	v := dhcpInfoInspectImpl{
		name: "doesn't matter",
		stateRecentHistory: []util.LogEntry{
			{Timestamp: 1, Content: "1"},
			{Timestamp: 2, Content: "2"},
		},
		stats: &dhcp.Stats{},
	}
	v.stats.PacketDiscardStats.InvalidPort.Init()
	v.stats.PacketDiscardStats.InvalidTransProto.Init()
	v.stats.PacketDiscardStats.InvalidPacketType.Init()
	for i := 0; i < invalidPortCount; i++ {
		v.stats.PacketDiscardStats.InvalidPort.Increment(invalidPort)
	}
	for i := 0; i < invalidTransProtoCount; i++ {
		v.stats.PacketDiscardStats.InvalidTransProto.Increment(invalidTransProto)
	}
	for i := 0; i < invalidPacketTypeCount; i++ {
		v.stats.PacketDiscardStats.InvalidPacketType.Increment(invalidPacketType)
	}
	for i := 0; i < invalidPacketType2Count; i++ {
		v.stats.PacketDiscardStats.InvalidPacketType.Increment(invalidPacketType2)
	}
	children := v.ListChildren()
	if diff := cmp.Diff([]string{
		"Stats", "DHCP State Recent History",
	}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}

	{
		// Validate Stats struct.
		child := v.GetChild("Stats")
		stats, ok := child.(*statCounterInspectImpl)

		if !ok {
			t.Fatalf("got GetChild(Stats) = %#v, want %T", child, (*statCounterInspectImpl)(nil))
		}

		expected := inspectNodeExpectation{
			node: inspect.Object{
				Name: "Stats",
			},
			children: []inspectNodeExpectation{
				{
					node: inspect.Object{
						Name: "PacketDiscardStats",
					},
					children: []inspectNodeExpectation{
						{
							node: inspect.Object{
								Name: "InvalidPort",
							},
							children: []inspectNodeExpectation{
								{
									node: inspect.Object{
										Name: strconv.Itoa(invalidPort),
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidPortCounter.Value(), 10))},
										},
									},
								},
								{
									node: inspect.Object{
										Name: "Total",
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidPortCounter.Value(), 10))},
										},
									},
								},
							},
						},
						{
							node: inspect.Object{
								Name: "InvalidTransProto",
							},
							children: []inspectNodeExpectation{
								{
									node: inspect.Object{
										Name: strconv.Itoa(invalidTransProto),
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidTransProtoCounter.Value(), 10))},
										},
									},
								},
								{
									node: inspect.Object{
										Name: "Total",
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidTransProtoCounter.Value(), 10))},
										},
									},
								},
							},
						},
						{
							node: inspect.Object{
								Name: "InvalidPacketType",
							},
							children: []inspectNodeExpectation{
								{
									node: inspect.Object{
										Name: strconv.Itoa(invalidPacketType),
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidPacketTypeCounter.Value(), 10))},
										},
									},
								},
								{
									node: inspect.Object{
										Name: strconv.Itoa(invalidPacketType2),
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidPacketType2Counter.Value(), 10))},
										},
									},
								},
								{
									node: inspect.Object{
										Name: "Total",
										Properties: []inspect.Property{
											{Key: counterPropertyKey, Value: inspect.PropertyValueWithStr(strconv.FormatUint(invalidPacketTypeCounter.Value()+invalidPacketType2Counter.Value(), 10))},
										},
									},
								},
							},
						},
					},
				},
			},
		}
		if err := checkInspectRecurse(stats, expected); err != nil {
			t.Error(err)
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}
	v.info.Config.Router = []tcpip.Address{ipv4Addr}
	v.info.Config.DNS = []tcpip.Address{ipv4Addr}
	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Properties: []inspect.Property{
			{Key: "State", Value: inspect.PropertyValueWithStr("initSelecting")},
			{Key: "AcquiredAddress", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "AssignedAddress", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "Acquisition", Value: inspect.PropertyValueWithStr("0s")},
			{Key: "Backoff", Value: inspect.PropertyValueWithStr("0s")},
			{Key: "Retransmission", Value: inspect.PropertyValueWithStr("0s")},
			{Key: "LeaseExpiration", Value: inspect.PropertyValueWithStr("m=+0.000000000")},
			{Key: "RenewTime", Value: inspect.PropertyValueWithStr("m=+0.000000000")},
			{Key: "RebindTime", Value: inspect.PropertyValueWithStr("m=+0.000000000")},
			{Key: "Config.ServerAddress", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "Config.SubnetMask", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "Config.Router0", Value: inspect.PropertyValueWithStr(ipv4Addr.String())},
			{Key: "Config.DNS0", Value: inspect.PropertyValueWithStr(ipv4Addr.String())},
			{Key: "Config.LeaseLength", Value: inspect.PropertyValueWithStr("0s")},
			{Key: "Config.RenewTime", Value: inspect.PropertyValueWithStr("0s")},
			{Key: "Config.RebindTime", Value: inspect.PropertyValueWithStr("0s")},
			{Key: "Config.Declined", Value: inspect.PropertyValueWithStr("false")},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}

	{
		child := v.GetChild("DHCP State Recent History")
		recentHistory, ok := child.(*circularLogsInspectImpl)
		if !ok {
			t.Errorf("got GetChild(DHCP State Recent History) %#v, want %T", child, (*statCounterInspectImpl)(nil))
		}

		if recentHistory != nil {
			expectedChildren := []string{"0", "1"}
			expectedChildrenData := []inspect.Object{
				{
					Name: "0",
					Properties: []inspect.Property{
						{Key: "@time", Value: inspect.PropertyValueWithStr("1")},
						{Key: "value", Value: inspect.PropertyValueWithStr("1")},
					},
				},
				{
					Name: "1",
					Properties: []inspect.Property{
						{Key: "@time", Value: inspect.PropertyValueWithStr("2")},
						{Key: "value", Value: inspect.PropertyValueWithStr("2")},
					},
				},
			}

			errors := circularLogsChecker(recentHistory, expectedChildren, expectedChildrenData)
			for _, err := range errors {
				t.Error(err)
			}
		}
	}
}

func TestEthInfoInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	const topopath, filepath = "topopath", "filepath"
	const features = ethernet.FeaturesWlan | ethernet.FeaturesSynthetic | ethernet.FeaturesLoopback

	device := ethernetext.Device{
		TB: t,
		GetInfoImpl: func() (ethernet.Info, error) {
			return ethernet.Info{
				Features: features,
			}, nil
		},
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				RxDepth: 1,
				TxDepth: 1,
			}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StopImpl: func() error {
			return nil
		},
		SetClientNameImpl: func(string) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}

	client, err := eth.NewClient("client", topopath, filepath, &device)
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := client.Close(); err != nil {
			t.Errorf("failed to close eth client: %s", err)
		}
		client.Wait()
	}()

	v := ethInfoInspectImpl{
		name:  "doesn't matter",
		value: client,
	}
	children := v.ListChildren()
	if diff := cmp.Diff([]string{
		"RxReads",
		"RxWrites",
		"TxReads",
		"TxWrites",
	}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*fifoStatsInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*fifoStatsInspectImpl)(nil))
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Metrics: []inspect.Metric{
			{Key: "TxDrops", Value: inspect.MetricValueWithUintValue(client.TxStats().Drops.Value())},
		},
		Properties: []inspect.Property{
			{Key: "Topopath", Value: inspect.PropertyValueWithStr(topopath)},
			{Key: "Filepath", Value: inspect.PropertyValueWithStr(filepath)},
			{Key: "Features", Value: inspect.PropertyValueWithStr(features.String())},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestFifoStatsInfoInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	tests := []struct {
		name     string
		impl     func() fifoStatsInspectImpl
		wantData func() inspect.Object
	}{
		{
			name: "size 2",
			impl: func() fifoStatsInspectImpl {
				var zeroCounter, nonZeroCounter tcpip.StatCounter
				nonZeroCounter.IncrementBy(5)
				return fifoStatsInspectImpl{
					name: "size 2",
					value: func(batch uint32) *tcpip.StatCounter {
						if batch%2 == 0 {
							return &zeroCounter
						}
						return &nonZeroCounter
					},
					size: 2,
				}
			},
			wantData: func() inspect.Object {
				return inspect.Object{
					Name: "size 2",
					Metrics: []inspect.Metric{
						{Key: "1", Value: inspect.MetricValueWithUintValue(5)},
					},
				}
			},
		},
		{
			name: "size 2001",
			impl: func() fifoStatsInspectImpl {
				var zeroCounter, nonZeroCounter tcpip.StatCounter
				nonZeroCounter.IncrementBy(5)
				return fifoStatsInspectImpl{
					name: "size 2001",
					value: func(batch uint32) *tcpip.StatCounter {
						if batch == 1 || batch == 2000 || batch == 2001 {
							return &nonZeroCounter
						}
						return &zeroCounter
					},
					size: 2001,
				}
			},
			wantData: func() inspect.Object {
				return inspect.Object{
					Name: "size 2001",
					Metrics: []inspect.Metric{
						{
							Key:   "1-2",
							Value: inspect.MetricValueWithUintValue(5),
						},
						{
							Key:   "1999-2000",
							Value: inspect.MetricValueWithUintValue(5),
						},
						{
							Key:   "2001",
							Value: inspect.MetricValueWithUintValue(5),
						},
					},
				}
			},
		},
		{
			name: "size 2048",
			impl: func() fifoStatsInspectImpl {
				var nonZeroCounter tcpip.StatCounter
				nonZeroCounter.IncrementBy(5)
				return fifoStatsInspectImpl{
					name: "size 2048",
					value: func(_ uint32) *tcpip.StatCounter {
						return &nonZeroCounter
					},
					size: 2048,
				}
			},
			wantData: func() inspect.Object {
				metrics := []inspect.Metric{}
				for i := 0; i < 2048; i = i + 2 {
					metrics = append(metrics, inspect.Metric{
						Key:   fmt.Sprintf("%d-%d", i+1, i+2),
						Value: inspect.MetricValueWithUintValue(10),
					})

				}
				return inspect.Object{
					Name:    "size 2048",
					Metrics: metrics,
				}
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			v := test.impl()

			// ListChildren always returns nil.
			children := v.ListChildren()
			if diff := cmp.Diff(children, []string(nil)); diff != "" {
				t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
			}

			// GetChild always returns nil.
			childName := "not a real child"
			if child := v.GetChild(childName); child != nil {
				t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
			}

			data := v.ReadData()
			if l := len(data.Metrics); l > maxMetricsForFifoStats {
				t.Errorf("the length of Metrics (%d) exceeds maxMetricsForFifoStats(%d)", l, maxMetricsForFifoStats)
			}
			if diff := cmp.Diff(test.wantData(), data, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
				t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestInspectGetMissingChild(t *testing.T) {
	addGoleakCheck(t)

	impl := inspectImpl{
		inner: &nicInfoMapInspectImpl{},
	}
	req, proxy, err := inspect.NewInspectWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("inspect.NewInspectWithCtxInterfaceRequest() = %s", err)
	}
	defer func() {
		if err := proxy.Close(); err != nil {
			t.Errorf("proxy.Close() = %s", err)
		}
	}()
	found, err := impl.OpenChild(context.Background(), "non-existing-child", req)
	if err != nil {
		t.Fatalf("impl.OpenChild(...) = %s", err)
	}
	if found {
		t.Fatalf("got impl.OpenChild(...) = true, want = false")
	}
	// The request channel must have been closed.
	if status := zx.Sys_object_wait_one(*proxy.Channel.Handle(), zx.SignalChannelPeerClosed, 0, nil); status != zx.ErrOk {
		t.Fatalf("zx.Sys_object_wait_one(_, zx.SignalChannelPeerClosed, 0, nil) = %s", status)
	}
}

func TestRoutingTableInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	impl := routingTableInspectImpl{
		value: []routes.ExtendedRoute{
			{}, {},
		},
	}
	children := impl.ListChildren()
	if diff := cmp.Diff([]string{
		"0", "1",
	}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := impl.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*routeInfoInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*routeInfoInspectImpl)(nil))
		}
	}

	childName := "not a real child"
	if got := impl.GetChild(childName); got != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, got)
	}

	// Index past the end of the routing table
	childName = "2"
	if got := impl.GetChild(childName); got != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, got)
	}
	if diff := cmp.Diff(inspect.Object{
		Name: "Routes",
	}, impl.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestRouteInfoInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	tests := []struct {
		name       string
		route      routes.ExtendedRoute
		properties []inspect.Property
	}{
		{
			name: "IPv4",
			route: routes.ExtendedRoute{
				Route: tcpip.Route{
					Destination: header.IPv4EmptySubnet,
					Gateway:     "\x01\x02\x03\x04",
					NIC:         1,
				},
				Metric:                42,
				MetricTracksInterface: true,
				Dynamic:               true,
				Enabled:               true,
			},
			properties: []inspect.Property{
				{Key: "Destination", Value: inspect.PropertyValueWithStr("0.0.0.0/0")},
				{Key: "Gateway", Value: inspect.PropertyValueWithStr("1.2.3.4")},
				{Key: "NIC", Value: inspect.PropertyValueWithStr("1")},
				{Key: "Metric", Value: inspect.PropertyValueWithStr("42")},
				{Key: "MetricTracksInterface", Value: inspect.PropertyValueWithStr("true")},
				{Key: "Dynamic", Value: inspect.PropertyValueWithStr("true")},
				{Key: "Enabled", Value: inspect.PropertyValueWithStr("true")},
			},
		},
		{
			name: "IPv6",
			route: routes.ExtendedRoute{
				Route: tcpip.Route{
					Destination: header.IPv6EmptySubnet,
					Gateway:     "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
					NIC:         2,
				},
				Metric:                0,
				MetricTracksInterface: false,
				Dynamic:               true,
				Enabled:               true,
			},
			properties: []inspect.Property{
				{Key: "Destination", Value: inspect.PropertyValueWithStr("::/0")},
				{Key: "Gateway", Value: inspect.PropertyValueWithStr("fe80::1")},
				{Key: "NIC", Value: inspect.PropertyValueWithStr("2")},
				{Key: "Metric", Value: inspect.PropertyValueWithStr("0")},
				{Key: "MetricTracksInterface", Value: inspect.PropertyValueWithStr("false")},
				{Key: "Dynamic", Value: inspect.PropertyValueWithStr("true")},
				{Key: "Enabled", Value: inspect.PropertyValueWithStr("true")},
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			impl := routeInfoInspectImpl{name: "0", value: test.route}

			children := impl.ListChildren()
			if diff := cmp.Diff(children, []string(nil)); diff != "" {
				t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
			}

			childName := "not a real child"
			if got := impl.GetChild(childName); got != nil {
				t.Errorf("got GetChild(%s) = %s, want = nil", childName, got)
			}

			if diff := cmp.Diff(
				inspect.Object{Name: "0", Properties: test.properties},
				impl.ReadData(),
				cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{}),
			); diff != "" {
				t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestNetworkEndpointStatsInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	const unknownNetworkProtocolNumber = 0
	impl := networkEndpointStatsInspectImpl{
		name: "Network Endpoint Stats",
		value: map[string]stack.NetworkEndpointStats{
			"IPv4":        &ipv4.Stats{},
			"IPv6":        &ipv6.Stats{},
			"ARP":         &arp.Stats{},
			"Random Name": &arp.Stats{},
		},
	}

	expectedProtocols := []string{"IPv4", "IPv6", "ARP", "Random Name"}

	children := impl.ListChildren()
	if diff := cmp.Diff(expectedProtocols, children, cmpopts.SortSlices(func(a, b string) bool {
		return a < b
	})); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}

	for _, childName := range children {
		if child := impl.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*statCounterInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*statCounterInspectImpl)(nil))
		}
	}

	childName := "not a real child"
	if got := impl.GetChild(childName); got != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, got)
	}

	if diff := cmp.Diff(inspect.Object{
		Name: "Network Endpoint Stats",
	}, impl.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestNeighborTableInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	someTime := tcpip.MonotonicTime{}
	impl := neighborTableInspectImpl{
		name: neighborsLabel,
		value: map[string]stack.NeighborEntry{
			ipv4Addr.String(): {
				Addr:      ipv4Addr,
				LinkAddr:  "\x0a\x00\x00\x00\x00\x01",
				State:     stack.Reachable,
				UpdatedAt: someTime.Add(1 * time.Nanosecond),
			},
			ipv6Addr.String(): {
				Addr:      ipv6Addr,
				LinkAddr:  "\x0a\x00\x00\x00\x00\x02",
				State:     stack.Stale,
				UpdatedAt: someTime.Add(2 * time.Nanosecond),
			},
		},
	}

	children := impl.ListChildren()
	if diff := cmp.Diff([]string{
		ipv4Addr.String(), ipv6Addr.String(),
	}, children, cmpopts.SortSlices(func(a, b string) bool {
		return a < b
	})); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := impl.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*neighborInfoInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %#v, want %T", childName, child, (*neighborInfoInspectImpl)(nil))
		}
	}

	childName := "not a real child"
	if got := impl.GetChild(childName); got != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, got)
	}

	if diff := cmp.Diff(inspect.Object{
		Name: neighborsLabel,
	}, impl.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestNeighborInfoInspectImpl(t *testing.T) {
	addGoleakCheck(t)

	someTime := tcpip.MonotonicTime{}
	tests := []struct {
		name     string
		neighbor stack.NeighborEntry
	}{
		{
			name: "IPv4",
			neighbor: stack.NeighborEntry{
				Addr:      ipv4Addr,
				LinkAddr:  "\x0a\x00\x00\x00\x00\x01",
				State:     stack.Reachable,
				UpdatedAt: someTime.Add(1 * time.Nanosecond),
			},
		},
		{
			name: "IPv6",
			neighbor: stack.NeighborEntry{
				Addr:      ipv6Addr,
				LinkAddr:  "\x0a\x00\x00\x00\x00\x02",
				State:     stack.Stale,
				UpdatedAt: someTime.Add(2 * time.Nanosecond),
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			impl := neighborInfoInspectImpl{value: test.neighbor}
			if diff := cmp.Diff(impl.ListChildren(), []string(nil)); diff != "" {
				t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
			}

			childName := "not a real child"
			if got := impl.GetChild(childName); got != nil {
				t.Errorf("got GetChild(%s) = %s, want = nil", childName, got)
			}

			if diff := cmp.Diff(
				inspect.Object{
					Name: test.neighbor.Addr.String(),
					Properties: []inspect.Property{
						{Key: "Link address", Value: inspect.PropertyValueWithStr(test.neighbor.LinkAddr.String())},
						{Key: "State", Value: inspect.PropertyValueWithStr(test.neighbor.State.String())},
					},
					Metrics: []inspect.Metric{
						{Key: "Last updated", Value: inspect.MetricValueWithIntValue(int64(fidlconv.ToZxTime(test.neighbor.UpdatedAt)))},
					},
				},
				impl.ReadData(),
				cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{}),
			); diff != "" {
				t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
