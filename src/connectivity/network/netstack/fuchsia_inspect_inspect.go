// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"reflect"
	"sort"
	"strconv"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"

	"app/context"
	"syslog"

	inspect "fidl/fuchsia/inspect/deprecated"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

// An infallible version of fuchsia.inspect.Inspect with FIDL details omitted.
type inspectInner interface {
	ReadData() inspect.Object
	ListChildren() []string
	GetChild(string) inspectInner
}

// An adapter that implements fuchsia.inspect.Inspect using the above.

var _ inspect.Inspect = (*inspectImpl)(nil)

type inspectImpl struct {
	inner inspectInner

	service *inspect.InspectService
}

func (impl *inspectImpl) ReadData() (inspect.Object, error) {
	return impl.inner.ReadData(), nil
}

func (impl *inspectImpl) ListChildren() ([]string, error) {
	return impl.inner.ListChildren(), nil
}

func (impl *inspectImpl) OpenChild(childName string, childChannel inspect.InspectInterfaceRequest) (bool, error) {
	if child := impl.inner.GetChild(childName); child != nil {
		svc := (&inspectImpl{
			inner:   child,
			service: impl.service,
		}).asService()
		return true, svc.AddFn(svc.Stub, childChannel.Channel)
	}
	return false, nil
}

func (impl *inspectImpl) asService() *context.Service {
	return &context.Service{
		Stub: &inspect.InspectStub{Impl: impl},
		AddFn: func(s fidl.Stub, c zx.Channel) error {
			_, err := impl.service.BindingSet.Add(s, c, nil)
			return err
		},
	}
}

// Inspect implementations are exposed as directories containing a node called "inspect".

var _ context.Directory = (*inspectDirectory)(nil)

type inspectDirectory struct {
	asService func() *context.Service
}

func (dir *inspectDirectory) Get(name string) (context.Node, bool) {
	if name == inspect.InspectName {
		return dir.asService(), true
	}
	return nil, false
}

func (dir *inspectDirectory) ForEach(fn func(string, context.Node)) {
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

type nicInfoMapInspectImpl struct {
	value map[tcpip.NICID]stack.NICInfo
}

func (impl *nicInfoMapInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: "NICs",
	}
}

func (impl *nicInfoMapInspectImpl) ListChildren() []string {
	var children []string
	for key := range impl.value {
		children = append(children, strconv.FormatUint(uint64(key), 10))
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
	value stack.NICInfo
}

func (impl *nicInfoInspectImpl) ReadData() inspect.Object {
	object := inspect.Object{
		Name: impl.name,
		Properties: []inspect.Property{
			{Key: "Name", Value: inspect.PropertyValueWithStr(impl.value.Name)},
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
	for _, protocolAddress := range impl.value.ProtocolAddresses {
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
			Key:   "ProtocolAddress",
			Value: inspect.PropertyValueWithStr(fmt.Sprintf("[%s] %s", protocol, protocolAddress.AddressWithPrefix)),
		})
	}
	return object
}

func (impl *nicInfoInspectImpl) ListChildren() []string {
	return []string{
		"Stats",
	}
}

func (impl *nicInfoInspectImpl) GetChild(childName string) inspectInner {
	switch childName {
	case "Stats":
		return &statCounterInspectImpl{
			name:  childName,
			value: reflect.ValueOf(impl.value.Stats),
		}
	default:
		return nil
	}
}

var _ inspectInner = (*socketInfoMapInspectImpl)(nil)

type socketInfoMapInspectImpl struct {
	value *sync.Map
}

func (impl *socketInfoMapInspectImpl) ReadData() inspect.Object {
	return inspect.Object{
		Name: "Socket Info",
	}
}

func (impl *socketInfoMapInspectImpl) ListChildren() []string {
	var children []string
	impl.value.Range(func(key, value interface{}) bool {
		children = append(children, strconv.FormatUint(key.(uint64), 10))
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
	if e, ok := impl.value.Load(id); ok {
		ep := e.(tcpip.Endpoint)
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
		"Stats",
	}
}

func (impl *socketInfoInspectImpl) GetChild(childName string) inspectInner {
	switch childName {
	case "Stats":
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
