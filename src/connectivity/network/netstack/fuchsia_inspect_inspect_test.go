// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"reflect"
	"sort"
	"strconv"
	"strings"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"

	"fidl/fuchsia/hardware/ethernet"
	inspect "fidl/fuchsia/inspect/deprecated"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

func TestStatCounterInspectImpl(t *testing.T) {
	s := tcpip.Stats{}.FillIn()
	v := statCounterInspectImpl{
		name:  "doesn't matter",
		value: reflect.ValueOf(s),
	}
	children := v.ListChildren()

	// Skip the IGMP child so that we can avoid a breaking change.
	//
	// TODO(65254): Remove once IGMP has rolled.
	for i, child := range children {
		if child == "IGMP" {
			children = append(children[:i], children[i+1:]...)
		}
	}

	// TODO(65254): Add IGMP to the list of children once IGMP has rolled.
	if diff := cmp.Diff([]string{
		"ICMP",
		"IP",
		"TCP",
		"UDP",
	}, children); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*statCounterInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &statCounterInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	s.UnknownProtocolRcvdPackets.IncrementBy(1)
	s.MalformedRcvdPackets.IncrementBy(2)
	s.DroppedPackets.IncrementBy(3)

	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Metrics: []inspect.Metric{
			{Key: "UnknownProtocolRcvdPackets", Value: inspect.MetricValueWithUintValue(s.UnknownProtocolRcvdPackets.Value())},
			{Key: "MalformedRcvdPackets", Value: inspect.MetricValueWithUintValue(s.MalformedRcvdPackets.Value())},
			{Key: "DroppedPackets", Value: inspect.MetricValueWithUintValue(s.DroppedPackets.Value())},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestSocketStatCounterInspectImpl(t *testing.T) {
	// Create a new netstack and add TCP and UDP endpoints.
	ns := newNetstack(t)
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
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &nicInfoInspectImpl{})
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
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &statCounterInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	v.value.Flags.Up = true
	v.value.Flags.Loopback = true
	v.value.dnsServers = []tcpip.Address{"\x01\x02\x03\x04"}

	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Properties: []inspect.Property{
			{Key: "Name", Value: inspect.PropertyValueWithStr(v.value.Name)},
			{Key: "NICID", Value: inspect.PropertyValueWithStr(strconv.FormatUint(uint64(v.value.nicid), 10))},
			{Key: "AdminUp", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.adminUp))},
			{Key: "LinkOnline", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.linkOnline))},
			{Key: "Up", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Up))},
			{Key: "Running", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Running))},
			{Key: "Loopback", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Loopback))},
			{Key: "Promiscuous", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Promiscuous))},
			{Key: "DNS server0", Value: inspect.PropertyValueWithStr(v.value.dnsServers[0].String())},
			{Key: "DHCP enabled", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.dhcpEnabled))},
		},
		Metrics: []inspect.Metric{
			{Key: "MTU", Value: inspect.MetricValueWithUintValue(uint64(v.value.MTU))},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestDHCPInfoInspectImpl(t *testing.T) {
	v := dhcpInfoInspectImpl{
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
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &statCounterInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Properties: []inspect.Property{
			{Key: "State", Value: inspect.PropertyValueWithStr(v.info.State.String())},
			{Key: "AcquiredAddress", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "ServerAddress", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "OldAddress", Value: inspect.PropertyValueWithStr("[none]")},
			{Key: "Acquisition", Value: inspect.PropertyValueWithStr(v.info.Acquisition.String())},
			{Key: "Backoff", Value: inspect.PropertyValueWithStr(v.info.Backoff.String())},
			{Key: "Retransmission", Value: inspect.PropertyValueWithStr(v.info.Retransmission.String())},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestEthInfoInspectImpl(t *testing.T) {
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
		_ = client.Close()
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
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &fifoStatsInspectImpl{})
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
	var zeroCounter, nonZeroCounter tcpip.StatCounter
	nonZeroCounter.IncrementBy(5)

	v := fifoStatsInspectImpl{
		name: "doesn't matter",
		value: func(depth uint32) *tcpip.StatCounter {
			if depth%2 == 0 {
				return &zeroCounter
			}
			return &nonZeroCounter
		},
		size: 2,
	}
	children := v.ListChildren()
	if diff := cmp.Diff(children, []string(nil)); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = nil", childName, child)
	}

	if diff := cmp.Diff(inspect.Object{
		Name: v.name,
		Metrics: []inspect.Metric{
			{Key: "1", Value: inspect.MetricValueWithUintValue(5)},
		},
	}, v.ReadData(), cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestInspectGetMissingChild(t *testing.T) {
	impl := inspectImpl{
		inner: &nicInfoMapInspectImpl{},
	}
	req, proxy, err := inspect.NewInspectWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("inspect.NewInspectWithCtxInterfaceRequest() = %s", err)
	}
	defer func() {
		if err := proxy.Close(); err != nil {
			t.Fatalf("proxy.Close() = %s", err)
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
	if _, err := zxwait.Wait(*proxy.Channel.Handle(), zx.SignalChannelPeerClosed, 0); err != nil {
		t.Fatalf("zxwait.Wait(_, zx.SignalChannelPeerClosed, 0) = %s", err)
	}
}

func TestRoutingTableInspectImpl(t *testing.T) {
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
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &routeInfoInspectImpl{})
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

func TestNeighborTableInspectImpl(t *testing.T) {
	ipv4Addr := tcpip.Address("\xc0\xa8\x01\x01")
	ipv6Addr := tcpip.Address("\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01")

	impl := neighborTableInspectImpl{
		name: neighborsLabel,
		value: map[string]stack.NeighborEntry{
			ipv4Addr.String(): {
				Addr:           ipv4Addr,
				LinkAddr:       "\x0a\x00\x00\x00\x00\x01",
				State:          stack.Reachable,
				UpdatedAtNanos: 1,
			},
			ipv6Addr.String(): {
				Addr:           ipv6Addr,
				LinkAddr:       "\x0a\x00\x00\x00\x00\x02",
				State:          stack.Stale,
				UpdatedAtNanos: 2,
			},
		},
	}

	children := impl.ListChildren()
	if diff := cmp.Diff([]string{
		ipv4Addr.String(), ipv6Addr.String(),
	}, children, cmpopts.SortSlices(func(a, b string) bool {
		return strings.Compare(a, b) < 0
	})); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := impl.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = nil, want non-nil", childName)
		} else if _, ok := child.(*neighborInfoInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &neighborInfoInspectImpl{})
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
	tests := []struct {
		name     string
		neighbor stack.NeighborEntry
	}{
		{
			name: "IPv4",
			neighbor: stack.NeighborEntry{
				Addr:           "\xc0\xa8\x01\x01",
				LinkAddr:       "\x0a\x00\x00\x00\x00\x01",
				State:          stack.Reachable,
				UpdatedAtNanos: 1,
			},
		},
		{
			name: "IPv6",
			neighbor: stack.NeighborEntry{
				Addr:           "\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
				LinkAddr:       "\x0a\x00\x00\x00\x00\x02",
				State:          stack.Stale,
				UpdatedAtNanos: 2,
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
						{Key: "Last updated", Value: inspect.MetricValueWithIntValue(test.neighbor.UpdatedAtNanos)},
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
