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
						GenOrdinal: types.Ordinal(314159),
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
						GenOrdinal: types.Ordinal(271828),
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
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "Test"},
					},
				},
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
						Ordinal:        1,
						OrdinalName:    "kTest_First_Ordinal",
						GenOrdinal:     314159,
						GenOrdinalName: "kTest_First_GenOrdinal",
						Name:           "First",
						HasRequest:     true,
						Request: []Parameter{
							{
								Type: Type{
									Decl:   "int16_t",
									LLDecl: "int16_t",
								},
								Name:   "Value",
								Offset: 0,
							},
						},
						RequestSize:         18,
						RequestTypeName:     "_TestFirstRequestTable",
						RequestMaxHandles:   0,
						HasResponse:         false,
						Response:            []Parameter{},
						ResponseSize:        0,
						ResponseTypeName:    "_TestFirstResponseTable",
						ResponseMaxHandles:  0,
						CallbackType:        "",
						ResponseHandlerType: "Test_First_ResponseHandler",
						ResponderType:       "Test_First_Responder",
						LLProps: LLProps{
							InterfaceName:      "Test",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							StackAllocRequest:  true,
							StackAllocResponse: true,
							EncodeRequest:      false,
							DecodeResponse:     false,
						},
					},
					{
						Ordinal:        2,
						OrdinalName:    "kTest_Second_Ordinal",
						GenOrdinal:     271828,
						GenOrdinalName: "kTest_Second_GenOrdinal",
						Name:           "Second",
						HasRequest:     true,
						Request: []Parameter{
							{
								Type: Type{
									Decl:   "::fidl::StringPtr",
									LLDecl: "::fidl::StringView",
									Dtor:   "~StringPtr",
								},
								Name:   "Value",
								Offset: 0,
							},
						},
						RequestSize:       32,
						RequestTypeName:   "_TestSecondRequestTable",
						RequestMaxHandles: 0,
						HasResponse:       true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:   "uint32_t",
									LLDecl: "uint32_t",
								},
								Name:   "Value",
								Offset: 0,
							},
						},
						ResponseSize:        20,
						ResponseTypeName:    "_TestSecondResponseTable",
						ResponseMaxHandles:  0,
						CallbackType:        "SecondCallback",
						ResponseHandlerType: "Test_Second_ResponseHandler",
						ResponderType:       "Test_Second_Responder",
						LLProps: LLProps{
							InterfaceName:      "Test",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							StackAllocRequest:  true,
							StackAllocResponse: true,
							EncodeRequest:      false,
							DecodeResponse:     false,
						},
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
				t.Fatalf("expected %+v\nactual %+v", ex.expected, *actual)
			}
		})
	}
}

func TestCompileTable(t *testing.T) {
	cases := []struct {
		name     string
		input    types.Table
		expected Table
	}{
		{
			name: "Basic",
			input: types.Table{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "Test"},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.TableMember{
					{
						Reserved: true,
						Ordinal:  1,
					},
					{
						Ordinal:  2,
						Name:     types.Identifier("second"),
						Reserved: false,
						Type: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
					},
				},
			},
			expected: Table{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "Test"},
					},
				},
				Namespace:      "::",
				Name:           "Test",
				TableType:      "_TestTable",
				BiggestOrdinal: 2,
				MaxHandles:     0,
				Members: []TableMember{
					{
						Type: Type{
							Decl:     "int64_t",
							LLDecl:   "int64_t",
							Dtor:     "",
							LLDtor:   "",
							DeclType: "",
						},
						Name:              "second",
						Ordinal:           2,
						FieldPresenceName: "has_second_",
						FieldDataName:     "second_value_",
						MethodHasName:     "has_second",
						MethodClearName:   "clear_second",
						ValueUnionName:    "ValueUnion_second",
					},
				},
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				Tables: []types.Table{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
			}
			result := Compile(root)
			actual, ok := result.Decls[0].(*Table)

			if !ok || actual == nil {
				t.Fatalf("decls[0] not an table, was instead %T", result.Decls[0])
			}
			if !reflect.DeepEqual(ex.expected, *actual) {
				t.Fatalf("expected %+v\nactual %+v", ex.expected, *actual)
			}
		})
	}
}

func addrOf(x int) *int {
	return &x
}

func TestCompileXUnion(t *testing.T) {
	cases := []struct {
		name     string
		input    types.XUnion
		expected XUnion
	}{
		{
			name: "SingleInt64",
			input: types.XUnion{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name: types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal: 0xdeadbeef,
						Type: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Name: types.Identifier("i"),
						Offset: 0,
						MaxOutOfLine: 0,
					},
				},
				Size: 24,
				MaxHandles: 0,
				MaxOutOfLine: 4294967295,
			},
			expected: XUnion{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name: types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Namespace: "::",
				Name: "Test",
				TableType: "_TestTable",
				Members: []XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal: 0xdeadbeef,
						Type: Type{
							Decl:     "int64_t",
							LLDecl:   "int64_t",
							Dtor:     "",
							LLDtor:   "",
							DeclType: "",
						},
						Name: "i",
						StorageName: "i_",
						TagName: "kI",
						Offset: 0,
					},
				},
				Size: 24,
				MaxHandles: 0,
				MaxOutOfLine: 4294967295,
			},
		},
		{
			name: "TwoArrays",
			input: types.XUnion{
				Attributes: types.Attributes{},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal: 0x11111111,
						Type: types.Type{
							Kind:         types.ArrayType,
							ElementType:  &types.Type{
								Kind:             types.PrimitiveType,
								PrimitiveSubtype: types.Int64,
							},
							ElementCount: addrOf(10),
						},
						Name: types.Identifier("i"),
						Offset: 0,
						MaxOutOfLine: 0,
					},
					{
						Attributes: types.Attributes{},
						Ordinal: 0x22222222,
						Type: types.Type{
							Kind:        types.ArrayType,
							ElementType: &types.Type{
								Kind:             types.PrimitiveType,
								PrimitiveSubtype: types.Int64,
							},
							ElementCount: addrOf(20),
						},
						Name: types.Identifier("j"),
						Offset: 0,
						MaxOutOfLine: 0,
					},
				},
				Size: 24,
				MaxHandles: 0,
				MaxOutOfLine: 4294967295,
			},
			expected: XUnion{
				Attributes: types.Attributes{},
				Namespace: "::",
				Name: "Test",
				TableType: "_TestTable",
				Members: []XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal: 0x11111111,
						Type: Type{
							Decl:     "::std::array<int64_t, 10>",
							LLDecl:   "::fidl::ArrayWrapper<int64_t, 10>",
							Dtor:     "~Array",
							LLDtor:   "~ArrayWrapper",
							DeclType: "",
						},
						Name: "i",
						StorageName: "i_",
						TagName: "kI",
						Offset: 0,
					},
					{
						Attributes: types.Attributes{},
						Ordinal: 0x22222222,
						Type: Type{
							Decl:     "::std::array<int64_t, 20>",
							LLDecl:   "::fidl::ArrayWrapper<int64_t, 20>",
							Dtor:     "~Array",
							LLDtor:   "~ArrayWrapper",
							DeclType: "",
						},
						Name: "j",
						StorageName: "j_",
						TagName: "kJ",
						Offset: 0,
					},
				},
				Size: 24,
				MaxHandles: 0,
				MaxOutOfLine: 4294967295,
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				XUnions: []types.XUnion{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
			}
			result := Compile(root)
			actual, ok := result.Decls[0].(*XUnion)

			if !ok || actual == nil {
				t.Fatalf("decls[0] not a xunion, was instead %T", result.Decls[0])
			}
			if !reflect.DeepEqual(ex.expected, *actual) {
				t.Fatalf("expected %+v\nactual %+v", ex.expected, *actual)
			}
		})
	}
}
