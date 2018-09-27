// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"reflect"
	"testing"

	"fidl/compiler/backend/types"
	. "fidl/compiler/backend/typestest"
)

func TestCompileInterface(t *testing.T) {
	cases := []struct {
		name     string
		input    types.Interface
		expected Interface
	}{
		{
			name: "Basic",
			input: types.Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "Test"},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Methods: []types.Method{
					{
						Ordinal:    types.Ordinal(1),
						Name:       types.Identifier("First"),
						HasRequest: true,
						Request: []types.Parameter{
							{
								Type: PrimitiveType(types.Int16),
								Name: types.Identifier("Value"),
							},
						},
						RequestSize: 18,
					},
					{
						Ordinal:    types.Ordinal(2),
						Name:       types.Identifier("Second"),
						HasRequest: true,
						Request: []types.Parameter{
							{
								Type: Nullable(StringType(nil)),
								Name: types.Identifier("Value"),
							},
						},
						RequestSize: 32,
						HasResponse: true,
						Response: []types.Parameter{
							{
								Type: PrimitiveType(types.Uint32),
								Name: types.Identifier("Value"),
							},
						},
						ResponseSize: 20,
					},
				},
			},
			//b EventSenderName: SyncName: SyncProxyName: Methods:[
			//Request:[{Type:{Decl:int16_t Dtor: DeclType:} Name:Value Offset:0}]
			//
			expected: Interface{
				Namespace:       "::",
				Name:            "Test",
				ClassName:       "Test_clazz",
				ServiceName:     "",
				ProxyName:       "Test_Proxy",
				StubName:        "Test_Stub",
				EventSenderName: "Test_EventSender",
				SyncName:        "Test_Sync",
				SyncProxyName:   "Test_SyncProxy",
				Methods: []Method{
					{
						Ordinal:     1,
						OrdinalName: "kTest_First_Ordinal",
						Name:        "First",
						HasRequest:  true,
						Request: []Parameter{
							{
								Type: Type{
									Decl: "int16_t",
								},
								Name:   "Value",
								Offset: 0,
							},
						},
						RequestSize:         18,
						RequestTypeName:     "_TestFirstRequestTable",
						HasResponse:         false,
						Response:            []Parameter{},
						ResponseSize:        0,
						ResponseTypeName:    "_TestFirstResponseTable",
						CallbackType:        "",
						ResponseHandlerType: "Test_First_ResponseHandler",
						ResponderType:       "Test_First_Responder",
					},
					{
						Ordinal:     2,
						OrdinalName: "kTest_Second_Ordinal",
						Name:        "Second",
						HasRequest:  true,
						Request: []Parameter{
							{
								Type: Type{
									Decl: "::fidl::StringPtr",
									Dtor: "~StringPtr",
								},
								Name:   "Value",
								Offset: 0,
							},
						},
						RequestSize:     32,
						RequestTypeName: "_TestSecondRequestTable",
						HasResponse:     true,
						Response: []Parameter{
							{
								Type: Type{
									Decl: "uint32_t",
								},
								Name:   "Value",
								Offset: 0,
							},
						},
						ResponseSize:        20,
						ResponseTypeName:    "_TestSecondResponseTable",
						CallbackType:        "SecondCallback",
						ResponseHandlerType: "Test_Second_ResponseHandler",
						ResponderType:       "Test_Second_Responder",
					},
				},
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				Interfaces: []types.Interface{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
			}
			result := Compile(root)
			actual, ok := result.Decls[0].(*Interface)
			if !ok || actual == nil {
				t.Fatalf("decls[0] not an interface, was instead %T", result.Decls[0])
			}
			if !reflect.DeepEqual(ex.expected, *actual) {
				t.Fatalf("expected %+v, actual %+v", ex.expected, *actual)
			}
		})
	}
}
