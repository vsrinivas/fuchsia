// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"encoding/binary"
	"fmt"
	"reflect"
	"syscall/zx"
	"syscall/zx/fidl"

	"app/context"

	"fidl/fuchsia/inspect"
	"github.com/google/netstack/tcpip"
)

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
			var value inspect.MetricValue
			value.SetUintValue(v.Field(i).Interface().(*tcpip.StatCounter).Value())
			metrics = append(metrics, inspect.Metric{
				Key:   field.Name,
				Value: value,
			})
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
			if field.Anonymous {
				names, err := (&statCounterInspectImpl{Value: v.Field(i)}).ListChildren()
				if err != nil {
					return nil, err
				}
				childNames = append(childNames, *names...)
			} else {
				childNames = append(childNames, field.Name)
			}
		default:
			panic(fmt.Sprintf("unexpected field %+v", field))
		}
	}
	return &childNames, nil
}

func (v *statCounterInspectImpl) OpenChild(childName string, childChannel inspect.InspectInterfaceRequest) (bool, error) {
	if v.Kind() != reflect.Struct {
		return false, nil
	}
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

func (v *statCounterInspectImpl) ForEach(fn func(string, context.Node)) {
	fn(inspect.InspectName, context.ServiceFn(v.bind))
}

var _ context.File = (*statCounterFile)(nil)

type statCounterFile struct {
	*tcpip.StatCounter
}

func (s *statCounterFile) GetBytes() []byte {
	var b [8]byte
	binary.LittleEndian.PutUint64(b[:], s.Value())
	return b[:]
}

type reflectNode struct {
	reflect.Value
}

var _ context.Directory = (*reflectNode)(nil)

func (v *reflectNode) Get(name string) (context.Node, bool) {
	if v.Kind() != reflect.Struct {
		return nil, false
	}
	if v := v.FieldByName(name); v.IsValid() {
		switch typ := v.Type(); typ.Kind() {
		case reflect.Struct:
			return &context.DirectoryWrapper{
				Directory: &reflectNode{
					Value: v,
				},
			}, true
		case reflect.Ptr:
			return &context.FileWrapper{
				File: &context.FileWrapper{
					File: &statCounterFile{StatCounter: v.Interface().(*tcpip.StatCounter)},
				},
			}, true
		default:
			panic(fmt.Sprintf("unexpected type %s", typ))
		}
	}
	return nil, false
}

func (v *reflectNode) ForEach(fn func(string, context.Node)) {
	typ := v.Type()
	for i := 0; i < v.NumField(); i++ {
		v := v.Field(i)
		switch field := typ.Field(i); field.Type.Kind() {
		case reflect.Struct:
			n := reflectNode{
				Value: v,
			}
			if field.Anonymous {
				n.ForEach(fn)
			} else {
				fn(field.Name, &context.DirectoryWrapper{
					Directory: &n,
				})
			}
		case reflect.Ptr:
			fn(field.Name, &context.FileWrapper{
				File: &statCounterFile{StatCounter: v.Interface().(*tcpip.StatCounter)},
			})
		default:
			panic(fmt.Sprintf("unexpected field %+v", field))
		}
	}
}
