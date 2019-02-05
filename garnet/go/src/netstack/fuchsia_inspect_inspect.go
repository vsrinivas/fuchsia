// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"reflect"

	"fidl/fuchsia/inspect"

	"github.com/google/netstack/tcpip"
)

// TODO(tamird): expose statsEP?

var _ inspect.Inspect = (*statCounterInspectImpl)(nil)

type statCounterInspectImpl struct {
	svc  *inspect.InspectService
	name string
	reflect.Value
}

func (v *statCounterInspectImpl) ReadData() (inspect.Object, error) {
	var metrics []inspect.Metric
	for i := 0; i < v.NumField(); i++ {
		switch t := v.Type().Field(i); t.Type.Kind() {
		case reflect.Struct:
		case reflect.Ptr:
			if s, ok := v.Field(i).Interface().(*tcpip.StatCounter); ok {
				var value inspect.MetricValue
				value.SetUintValue(s.Value())
				metrics = append(metrics, inspect.Metric{
					Key:   t.Name,
					Value: value,
				})
			}
		default:
			panic(fmt.Sprintf("unexpected field %+v", t))
		}
	}
	return inspect.Object{
		Name:    v.name,
		Metrics: &metrics,
	}, nil
}

func (v *statCounterInspectImpl) ListChildren() (*[]string, error) {
	var childNames []string
	for i := 0; i < v.NumField(); i++ {
		switch t := v.Type().Field(i); t.Type.Kind() {
		case reflect.Ptr:
		case reflect.Struct:
			childNames = append(childNames, t.Name)
		default:
			panic(fmt.Sprintf("unexpected field %+v", t))
		}
	}
	return &childNames, nil
}

func (v *statCounterInspectImpl) OpenChild(childName string, childChannel inspect.InspectInterfaceRequest) (bool, error) {
	svc := v.svc
	if v := v.FieldByName(childName); v.IsValid() {
		_, err := svc.Add(&statCounterInspectImpl{
			svc:   svc,
			name:  childName,
			Value: v,
		}, childChannel.ToChannel(), nil)
		return true, err
	}
	return false, nil
}
