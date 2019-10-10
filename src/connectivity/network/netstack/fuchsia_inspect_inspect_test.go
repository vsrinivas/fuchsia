// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"reflect"
	"strconv"
	"testing"

	inspect "fidl/fuchsia/inspect/deprecated"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/stack"
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

func TestNicInfoMapInspectImpl(t *testing.T) {
	v := nicInfoMapInspectImpl{
		value: map[tcpip.NICID]stack.NICInfo{
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

	if diff := cmp.Diff(v.ReadData(), inspect.Object{
		Name: v.name,
		Properties: []inspect.Property{
			{Key: "Name", Value: inspect.PropertyValueWithStr(v.value.Name)},
			{Key: "Up", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Up))},
			{Key: "Running", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Running))},
			{Key: "Loopback", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Loopback))},
			{Key: "Promiscuous", Value: inspect.PropertyValueWithStr(strconv.FormatBool(v.value.Flags.Promiscuous))},
		},
		Metrics: []inspect.Metric{
			{Key: "MTU", Value: inspect.MetricValueWithUintValue(uint64(v.value.MTU))},
		},
	}, cmpopts.IgnoreUnexported(inspect.Object{}, inspect.Property{}, inspect.Metric{})); diff != "" {
		t.Errorf("ReadData() mismatch (-want +got):\n%s", diff)
	}
}
