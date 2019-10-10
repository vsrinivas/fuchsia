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

	"app/context"
	"syslog"

	inspect "fidl/fuchsia/inspect/deprecated"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/stack"
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
			switch t := v.Interface().(type) {
			case statCounter:
				metrics = append(metrics, inspect.Metric{
					Key:   field.Name,
					Value: inspect.MetricValueWithUintValue(t.Value()),
				})
			default:
				if field.Anonymous && v.Kind() == reflect.Struct {
					metrics = append(metrics, (&statCounterInspectImpl{value: v}).asMetrics()...)
				}
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
		if field := typ.Field(i); len(field.PkgPath) == 0 && field.Type.Kind() == reflect.Struct {
			v := impl.value.Field(i)
			if field.Anonymous {
				children = append(children, (&statCounterInspectImpl{value: v}).ListChildren()...)
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
