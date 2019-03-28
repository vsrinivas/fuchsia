// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"reflect"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/io"

	"app/context"

	"fidl/fuchsia/inspect"

	"github.com/google/netstack/tcpip"
)

// TODO(tamird): expose statsEP?

var _ inspect.Inspect = (*statCounterInspectImpl)(nil)

type statCounterInspectImpl struct {
	name string
	reflect.Value
}

func (v *statCounterInspectImpl) bind(c zx.Channel) error {
	b := fidl.Binding{
		Stub:    &inspect.InspectStub{Impl: v},
		Channel: c,
	}
	return b.Init(func(error) {
		if err := b.Close(); err != nil {
			panic(err)
		}
	})
}

func (v *statCounterInspectImpl) ReadData() (inspect.Object, error) {
	var metrics []inspect.Metric
	typ := v.Type()
	for i := 0; i < v.NumField(); i++ {
		switch field := typ.Field(i); field.Type.Kind() {
		case reflect.Struct:
		case reflect.Ptr:
			if s, ok := v.Field(i).Interface().(*tcpip.StatCounter); ok {
				var value inspect.MetricValue
				value.SetUintValue(s.Value())
				metrics = append(metrics, inspect.Metric{
					Key:   field.Name,
					Value: value,
				})
			}
		default:
			panic(fmt.Sprintf("unexpected field %+v", field))
		}
	}
	return inspect.Object{
		Name:    v.name,
		Metrics: &metrics,
	}, nil
}

func (v *statCounterInspectImpl) ListChildren() (*[]string, error) {
	var childNames []string
	typ := v.Type()
	for i := 0; i < v.NumField(); i++ {
		switch field := typ.Field(i); field.Type.Kind() {
		case reflect.Ptr:
		case reflect.Struct:
			childNames = append(childNames, field.Name)
		default:
			panic(fmt.Sprintf("unexpected field %+v", field))
		}
	}
	return &childNames, nil
}

func (v *statCounterInspectImpl) OpenChild(childName string, childChannel inspect.InspectInterfaceRequest) (bool, error) {
	if v := v.FieldByName(childName); v.IsValid() {
		return true, (&statCounterInspectImpl{
			name:  childName,
			Value: v,
		}).bind(childChannel.Channel)
	}
	return false, nil
}

var _ context.Directory = (*statCounterInspectImpl)(nil)

func (v *statCounterInspectImpl) Get(name string) (context.Node, bool) {
	if name == inspect.InspectName {
		return context.ServiceFn(v.bind), true
	}
	return nil, false
}

func (v *statCounterInspectImpl) ForEach(fn func(string, io.Node)) {
	fn(inspect.InspectName, context.ServiceFn(v.bind))
}
