// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"reflect"
)

// enumMember should be the base type of all namespaced enumeration types.
type namespacedEnumMember int

var enumMemberBaseType = reflect.TypeOf(namespacedEnumMember(0))

// namespacedEnum helps to create a group of values nested under one container
// object, such that the entire group can be easily injected into templates
// by injecting the container object, and referenced using the
// `{{ MyEnum.MyEnumMember }}` pattern.
//
// It requires that `namespace` is a struct used as container for enumerations,
// and assigns each enum member field an ordinal increasing from 1.
//
// Every enum member must be exported, and the struct should only contain fields
// of the same enum member type.
//
// Here is an example:
//
// Defining:
//
//     type color namespacedEnumMember
//     type colors struct {
//         Red   color
//         Green color
//         Blue  color
//     }
//     Colors := namespacedEnum(colors{}).(colors)
//
// Using:
//
//     // Instantiating the template
//     template.FuncMap{
//         "Colors": func() interface{} { return Colors },
//     }
//
//     // Authoring the template
//     {{ if eq .Color Colors.Red }} ... {{ end }}
//
func namespacedEnum(namespace interface{}) interface{} {
	ns := reflect.New(reflect.TypeOf(namespace)).Elem()
	if ns.Kind() != reflect.Struct {
		panic(fmt.Sprintf("Must use a struct as namespace. Got %v", ns.Kind()))
	}
	var fieldType reflect.Type
	for i := 0; i < ns.NumField(); i++ {
		f := ns.Field(i)
		if !f.Type().ConvertibleTo(enumMemberBaseType) ||
			f.Type() == enumMemberBaseType {
			panic(fmt.Sprintf("Each struct field type must be an alias of %s", enumMemberBaseType))
		}
		if fieldType == nil {
			fieldType = f.Type()
		} else if fieldType != f.Type() {
			panic("Each struct field must be of the same type")
		}
		v := reflect.New(f.Type()).Elem()
		v.SetInt(int64(i + 1))
		f.Set(v)
	}
	return ns.Interface()
}
