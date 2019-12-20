// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"reflect"
	"sort"
	"strconv"
	"syscall/zx"
	"testing"

	inspect "fidl/fuchsia/inspect/deprecated"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
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
	if diff := cmp.Diff(children, []string{
		"ICMP",
		"IP",
		"TCP",
		"UDP",
	}); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = %v, want non-nil", childName, child)
		} else if _, ok := child.(*statCounterInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &statCounterInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = %v", childName, child, nil)
	}

	s.UnknownProtocolRcvdPackets.IncrementBy(1)
	s.MalformedRcvdPackets.IncrementBy(2)
	s.DroppedPackets.IncrementBy(3)

	if diff := cmp.Diff(v.ReadData(), inspect.Object{
		Name: v.name,
		Metrics: []inspect.Metric{
			{Key: "UnknownProtocolRcvdPackets", Value: inspect.MetricValueWithUintValue(s.UnknownProtocolRcvdPackets.Value())},
			{Key: "MalformedRcvdPackets", Value: inspect.MetricValueWithUintValue(s.MalformedRcvdPackets.Value())},
			{Key: "DroppedPackets", Value: inspect.MetricValueWithUintValue(s.DroppedPackets.Value())},
		},
	}, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestSocketStatCounterInspectImpl(t *testing.T) {
	// Create a new netstack and add TCP and UDP endpoints.
	ns := newNetstack(t)
	wq := new(waiter.Queue)
	// Grab locks just in case the callee has any isLock checks.
	ns.mu.Lock()
	tcpEP, err := ns.mu.stack.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		t.Fatal(err)
	}
	udpEP, err := ns.mu.stack.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		t.Fatal(err)
	}
	ns.mu.Unlock()
	v := socketInfoMapInspectImpl{
		value: &ns.endpoints,
	}
	children := v.ListChildren()
	if diff := cmp.Diff(children, []string(nil)); diff != "" {
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
	if diff := cmp.Diff(children, []string{strconv.Itoa(key1), strconv.Itoa(key2)}); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = %v", childName, child, nil)
	}

	for _, name := range children {
		child := v.GetChild(name)
		if child == nil {
			t.Fatalf("got GetChild(%s) = %v, want non-nil", name, child)
		}

		var protoName string
		var expectInspectObj inspect.Object
		// Update protocol specific expected values.
		val, err := strconv.ParseUint(name, 10, 64)
		if err != nil {
			t.Fatalf("string parsing error %s", err)
		}
		switch val {
		case key1:
			protoName = "TCP"
			expectInspectObj = inspect.Object{
				Name: "Stats",
				Metrics: []inspect.Metric{
					{Key: "SegmentsReceived", Value: inspect.MetricValueWithUintValue(0)},
					{Key: "SegmentsSent", Value: inspect.MetricValueWithUintValue(0)},
					{Key: "FailedConnectionAttempts", Value: inspect.MetricValueWithUintValue(0)},
				},
			}
		case key2:
			protoName = "UDP"
			expectInspectObj = inspect.Object{
				Name: "Stats",
				Metrics: []inspect.Metric{
					{Key: "PacketsReceived", Value: inspect.MetricValueWithUintValue(0)},
					{Key: "PacketsSent", Value: inspect.MetricValueWithUintValue(0)},
				},
			}
		}

		epChildren := child.ListChildren()
		if diff := cmp.Diff(epChildren, []string{
			"Stats",
		}); diff != "" {
			t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
		}
		for _, c := range epChildren {
			if c == "Stats" {
				statsChild := child.GetChild(c)
				if diff := cmp.Diff(statsChild.ListChildren(), []string{
					"ReceiveErrors",
					"ReadErrors",
					"SendErrors",
					"WriteErrors",
				}); diff != "" {
					t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
				}

				// Compare against the expected inspect objects.
				if diff := cmp.Diff(statsChild.ReadData(), expectInspectObj, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{}, inspect.Property{})); diff != "" {
					t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
				}
			}
		}

		if diff := cmp.Diff(child.ReadData(), inspect.Object{
			Name: name,
			Properties: []inspect.Property{
				{Key: "NetworkProtocol", Value: inspect.PropertyValueWithStr("IPv4")},
				{Key: "TransportProtocol", Value: inspect.PropertyValueWithStr(protoName)},
				{Key: "State", Value: inspect.PropertyValueWithStr("INITIAL")},
				{Key: "LocalAddress", Value: inspect.PropertyValueWithStr(":0")},
				{Key: "RemoteAddress", Value: inspect.PropertyValueWithStr(":0")},
				{Key: "BindAddress", Value: inspect.PropertyValueWithStr("")},
				{Key: "BindNICID", Value: inspect.PropertyValueWithStr("0")},
				{Key: "RegisterNICID", Value: inspect.PropertyValueWithStr("0")},
			},
		}, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Metric{}, inspect.Property{})); diff != "" {
			t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
		}
	}

	// Empty the endpoints.
	ns.endpoints.Range(func(handle zx.Handle, _ tcpip.Endpoint) bool {
		ns.endpoints.Delete(handle)
		return true
	})
	children = v.ListChildren()
	if diff := cmp.Diff(children, []string(nil)); diff != "" {
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
	if diff := cmp.Diff(children, []string{
		"1", "2",
	}); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = %v, want non-nil", childName, child)
		} else if _, ok := child.(*nicInfoInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &nicInfoInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = %v", childName, child, nil)
	}

	if diff := cmp.Diff(v.ReadData(), inspect.Object{
		Name: "NICs",
	}, cmpopts.IgnoreUnexported(inspect.Object{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestNicInfoInspectImpl(t *testing.T) {
	v := nicInfoInspectImpl{
		name: "doesn't matter",
	}
	children := v.ListChildren()
	if diff := cmp.Diff(children, []string{
		"Stats",
	}); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = %v, want non-nil", childName, child)
		} else if _, ok := child.(*statCounterInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &statCounterInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = %v", childName, child, nil)
	}

	v.value.Flags.Up = true
	v.value.Flags.Loopback = true
	v.value.dnsServers = []tcpip.Address{"\x01\x02\x03\x04"}

	if diff := cmp.Diff(v.ReadData(), inspect.Object{
		Name: v.name,
		Properties: []inspect.Property{
			{Key: "Name", Value: inspect.PropertyValueWithStr(v.value.Name)},
			{Key: "Up", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Up))},
			{Key: "Running", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Running))},
			{Key: "Loopback", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Loopback))},
			{Key: "Promiscuous", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Promiscuous))},
			{Key: "Filepath", Value: inspect.PropertyValueWithStr(v.value.filepath)},
			{Key: "Features", Value: inspect.PropertyValueWithStr(featuresString(v.value.features))},
			{Key: "DNS server0", Value: inspect.PropertyValueWithStr(v.value.dnsServers[0].String())},
			{Key: "DHCP enabled", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.dhcpEnabled))},
		},
		Metrics: []inspect.Metric{
			{Key: "MTU", Value: inspect.MetricValueWithUintValue(uint64(v.value.MTU))},
		},
	}, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}

func TestDHCPInfoInspectImpl(t *testing.T) {
	v := dhcpInfoInspectImpl{
		name: "doesn't matter",
	}
	children := v.ListChildren()
	if diff := cmp.Diff(children, []string{
		"Stats",
	}); diff != "" {
		t.Errorf("ListChildren() mismatch (-want +got):\n%s", diff)
	}
	for _, childName := range children {
		if child := v.GetChild(childName); child == nil {
			t.Errorf("got GetChild(%s) = %v, want non-nil", childName, child)
		} else if _, ok := child.(*statCounterInspectImpl); !ok {
			t.Errorf("got GetChild(%s) = %T, want %T", childName, child, &statCounterInspectImpl{})
		}
	}

	childName := "not a real child"
	if child := v.GetChild(childName); child != nil {
		t.Errorf("got GetChild(%s) = %s, want = %v", childName, child, nil)
	}

	if diff := cmp.Diff(v.ReadData(), inspect.Object{
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
	}, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}
