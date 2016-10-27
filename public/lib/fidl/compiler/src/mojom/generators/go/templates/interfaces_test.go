// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"testing"

	"mojom/generators/go/translator"
)

func testInterfaceTemplate() translator.InterfaceTemplate {
	return translator.InterfaceTemplate{
		Name: "SomeInterface",
		Methods: []translator.MethodTemplate{
			{
				MethodName: "FirstMethod",
				FullName:   "someInterface_FirstMethod",
				Ordinal:    10,
				Params: translator.StructTemplate{
					Fields: []translator.StructFieldTemplate{
						{Name: "Foo", Type: "uint32"},
						{Name: "Bar", Type: "string"},
					},
				},
			},
			{
				MethodName: "SecondMethod",
				FullName:   "someInterface_SecondMethod",
				Ordinal:    21,
				Params: translator.StructTemplate{
					Fields: []translator.StructFieldTemplate{
						{Name: "Foo", Type: "uint32"},
					},
				},
				ResponseParams: &translator.StructTemplate{
					Fields: []translator.StructFieldTemplate{
						{Name: "RetArg", Type: "uint32"},
					},
				},
			},
		},
	}
}

func TestInterfaceInterfaceDecl(t *testing.T) {
	expected := `type SomeInterface interface {
	FirstMethod(inFoo uint32, inBar string) (err error)
	SecondMethod(inFoo uint32) (outRetArg uint32, err error)
}`

	i := testInterfaceTemplate()

	check(t, expected, "InterfaceInterfaceDecl", i)
}

func TestMethodOrdinals(t *testing.T) {
	expected := `const someInterface_FirstMethod_Ordinal uint32 = 10
const someInterface_SecondMethod_Ordinal uint32 = 21
`

	i := testInterfaceTemplate()

	check(t, expected, "MethodOrdinals", i)
}

func TestServiceName(t *testing.T) {
	expected := `const someInterface_Name string = "SomeService"

func (r *SomeInterface_Request) Name() string {
	return someInterface_Name
}

func (p *SomeInterface_Pointer) Name() string {
	return someInterface_Name
}

func (f *SomeInterface_ServiceFactory) Name() string {
	return someInterface_Name
}`

	serviceName := "SomeService"
	i := translator.InterfaceTemplate{
		Name:        "SomeInterface",
		PrivateName: "someInterface",
		ServiceName: &serviceName,
	}

	check(t, expected, "ServiceDecl", i)
}
