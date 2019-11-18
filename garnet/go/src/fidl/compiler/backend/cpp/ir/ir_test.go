// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"testing"

	"github.com/google/go-cmp/cmp"

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
						Ordinal:    1,
						GenOrdinal: 314159,
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
						Ordinal:    2,
						GenOrdinal: 271828,
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
				Namespace:           "::",
				Name:                "Test",
				ClassName:           "Test_clazz",
				ServiceName:         "",
				ProxyName:           "Test_Proxy",
				StubName:            "Test_Stub",
				EventSenderName:     "Test_EventSender",
				SyncName:            "Test_Sync",
				SyncProxyName:       "Test_SyncProxy",
				RequestEncoderName:  "Test_RequestEncoder",
				RequestDecoderName:  "Test_RequestDecoder",
				ResponseEncoderName: "Test_ResponseEncoder",
				ResponseDecoderName: "Test_ResponseDecoder",
				HasEvents:           false,
				Methods: []Method{
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    1,
								GenOrdinal: 314159,
							},
							"kTest_First_Ordinal",
							"kTest_First_GenOrdinal",
						),
						Name:                 "First",
						NameInLowerSnakeCase: "first",
						HasRequest:           true,
						Request: []Parameter{
							{
								Type: Type{
									Decl:        "int16_t",
									LLDecl:      "int16_t",
									IsPrimitive: true,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						RequestSize:         18,
						RequestTypeName:     "_TestFirstRequestTable",
						V1RequestTypeName:   "v1__TestFirstRequestTable",
						RequestMaxHandles:   0,
						HasResponse:         false,
						Response:            []Parameter{},
						ResponseSize:        0,
						ResponseTypeName:    "_TestFirstResponseTable",
						V1ResponseTypeName:  "v1__TestFirstResponseTable",
						ResponseMaxHandles:  0,
						CallbackType:        "",
						ResponseHandlerType: "Test_First_ResponseHandler",
						ResponderType:       "Test_First_Responder",
						LLProps: LLProps{
							InterfaceName:      "Test",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
						},
					},
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    2,
								GenOrdinal: 271828,
							},
							"kTest_Second_Ordinal",
							"kTest_Second_GenOrdinal",
						),
						Name:                 "Second",
						NameInLowerSnakeCase: "second",
						HasRequest:           true,
						Request: []Parameter{
							{
								Type: Type{
									Decl:        "::fidl::StringPtr",
									LLDecl:      "::fidl::StringView",
									Dtor:        "~StringPtr",
									IsPrimitive: false,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						RequestSize:       32,
						RequestTypeName:   "_TestSecondRequestTable",
						V1RequestTypeName: "v1__TestSecondRequestTable",
						RequestMaxHandles: 0,
						HasResponse:       true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:        "uint32_t",
									LLDecl:      "uint32_t",
									IsPrimitive: true,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						ResponseSize:        20,
						ResponseTypeName:    "_TestSecondResponseTable",
						V1ResponseTypeName:  "v1__TestSecondResponseTable",
						ResponseMaxHandles:  0,
						CallbackType:        "SecondCallback",
						ResponseHandlerType: "Test_Second_ResponseHandler",
						ResponderType:       "Test_Second_Responder",
						LLProps: LLProps{
							InterfaceName:      "Test",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           52,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           52,
							},
						},
					},
				},
			},
		},
		{
			name: "Events",
			input: types.Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Name: types.EncodedCompoundIdentifier("EventTest"),
				Methods: []types.Method{
					{
						Ordinal:     1,
						GenOrdinal:  314159,
						Name:        types.Identifier("First"),
						HasResponse: true,
						Response: []types.Parameter{
							{
								Type: PrimitiveType(types.Int16),
								Name: types.Identifier("Value"),
							},
						},
						ResponseSize: 18,
					},
				},
			},
			expected: Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Namespace:           "::",
				Name:                "EventTest",
				ClassName:           "EventTest_clazz",
				ServiceName:         "",
				ProxyName:           "EventTest_Proxy",
				StubName:            "EventTest_Stub",
				EventSenderName:     "EventTest_EventSender",
				SyncName:            "EventTest_Sync",
				SyncProxyName:       "EventTest_SyncProxy",
				RequestEncoderName:  "EventTest_RequestEncoder",
				RequestDecoderName:  "EventTest_RequestDecoder",
				ResponseEncoderName: "EventTest_ResponseEncoder",
				ResponseDecoderName: "EventTest_ResponseDecoder",
				HasEvents:           true,
				Methods: []Method{
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    1,
								GenOrdinal: 314159,
							},
							"kEventTest_First_Ordinal",
							"kEventTest_First_GenOrdinal",
						),
						Name:                 "First",
						NameInLowerSnakeCase: "first",
						Request:              []Parameter{},
						RequestSize:          0,
						RequestTypeName:      "_EventTestFirstRequestTable",
						V1RequestTypeName:    "v1__EventTestFirstRequestTable",
						RequestMaxHandles:    0,
						HasResponse:          true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:        "int16_t",
									LLDecl:      "int16_t",
									IsPrimitive: true,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						ResponseSize:        18,
						ResponseTypeName:    "_EventTestFirstEventTable",
						V1ResponseTypeName:  "v1__EventTestFirstEventTable",
						ResponseMaxHandles:  0,
						CallbackType:        "FirstCallback",
						ResponseHandlerType: "EventTest_First_ResponseHandler",
						ResponderType:       "EventTest_First_Responder",
						LLProps: LLProps{
							InterfaceName:      "EventTest",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
						},
					},
				},
			},
		},
		{
			name: "EventsTooBigForStack",
			input: types.Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Name: types.EncodedCompoundIdentifier("EventTest"),
				Methods: []types.Method{
					{
						Ordinal:     2,
						GenOrdinal:  271828,
						Name:        types.Identifier("Second"),
						HasResponse: true,
						Response: []types.Parameter{
							{
								Type: types.Type{
									Kind: types.ArrayType,
									ElementType: &types.Type{
										Kind:             types.PrimitiveType,
										PrimitiveSubtype: types.Int64,
									},
									ElementCount: addrOf(8000),
								},
								Name: types.Identifier("Value"),
							},
						},
						ResponseSize: 64016,
					},
				},
			},
			expected: Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Namespace:           "::",
				Name:                "EventTest",
				ClassName:           "EventTest_clazz",
				ServiceName:         "",
				ProxyName:           "EventTest_Proxy",
				StubName:            "EventTest_Stub",
				EventSenderName:     "EventTest_EventSender",
				SyncName:            "EventTest_Sync",
				SyncProxyName:       "EventTest_SyncProxy",
				RequestEncoderName:  "EventTest_RequestEncoder",
				RequestDecoderName:  "EventTest_RequestDecoder",
				ResponseEncoderName: "EventTest_ResponseEncoder",
				ResponseDecoderName: "EventTest_ResponseDecoder",
				HasEvents:           true,
				Methods: []Method{
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    2,
								GenOrdinal: 271828,
							},
							"kEventTest_Second_Ordinal",
							"kEventTest_Second_GenOrdinal",
						),
						Name:                 "Second",
						NameInLowerSnakeCase: "second",
						HasRequest:           false,
						Request:              []Parameter{},
						RequestSize:          0,
						RequestTypeName:      "_EventTestSecondRequestTable",
						V1RequestTypeName:    "v1__EventTestSecondRequestTable",
						RequestMaxHandles:    0,
						HasResponse:          true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:        "::std::array<int64_t, 8000>",
									LLDecl:      "::fidl::Array<int64_t, 8000>",
									Dtor:        "~array",
									LLDtor:      "~Array",
									IsPrimitive: false,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						ResponseSize:        64016,
						ResponseTypeName:    "_EventTestSecondEventTable",
						V1ResponseTypeName:  "v1__EventTestSecondEventTable",
						ResponseMaxHandles:  0,
						CallbackType:        "SecondCallback",
						ResponseHandlerType: "EventTest_Second_ResponseHandler",
						ResponderType:       "EventTest_Second_Responder",
						LLProps: LLProps{
							InterfaceName:      "EventTest",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: false,
								StackUse:           0,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: false,
								StackUse:           0,
							},
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
			if diff := cmp.Diff(ex.expected, *actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}
func TestCompileInterfaceLLCPP(t *testing.T) {
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
						Ordinal:    1,
						GenOrdinal: 314159,
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
						Ordinal:    2,
						GenOrdinal: 271828,
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
				Namespace:           "::llcpp::",
				Name:                "Test",
				ClassName:           "Test_clazz",
				ServiceName:         "",
				ProxyName:           "Test_Proxy",
				StubName:            "Test_Stub",
				EventSenderName:     "Test_EventSender",
				SyncName:            "Test_Sync",
				SyncProxyName:       "Test_SyncProxy",
				RequestEncoderName:  "Test_RequestEncoder",
				RequestDecoderName:  "Test_RequestDecoder",
				ResponseEncoderName: "Test_ResponseEncoder",
				ResponseDecoderName: "Test_ResponseDecoder",
				HasEvents:           false,
				Methods: []Method{
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    1,
								GenOrdinal: 314159,
							},
							"kTest_First_Ordinal",
							"kTest_First_GenOrdinal",
						),
						Name:                 "First",
						NameInLowerSnakeCase: "first",
						HasRequest:           true,
						Request: []Parameter{
							{
								Type: Type{
									Decl:        "int16_t",
									LLDecl:      "int16_t",
									IsPrimitive: true,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						RequestSize:         18,
						RequestTypeName:     "_TestFirstRequestTable",
						V1RequestTypeName:   "v1__TestFirstRequestTable",
						RequestMaxHandles:   0,
						HasResponse:         false,
						Response:            []Parameter{},
						ResponseSize:        0,
						ResponseTypeName:    "_TestFirstResponseTable",
						V1ResponseTypeName:  "v1__TestFirstResponseTable",
						ResponseMaxHandles:  0,
						CallbackType:        "",
						ResponseHandlerType: "Test_First_ResponseHandler",
						ResponderType:       "Test_First_Responder",
						LLProps: LLProps{
							InterfaceName:      "Test",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
						},
					},
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    2,
								GenOrdinal: 271828,
							},
							"kTest_Second_Ordinal",
							"kTest_Second_GenOrdinal",
						),
						Name:                 "Second",
						NameInLowerSnakeCase: "second",
						HasRequest:           true,
						Request: []Parameter{
							{
								Type: Type{
									Decl:        "::fidl::StringPtr",
									LLDecl:      "::fidl::StringView",
									Dtor:        "~StringPtr",
									IsPrimitive: false,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						RequestSize:       32,
						RequestTypeName:   "_TestSecondRequestTable",
						V1RequestTypeName: "v1__TestSecondRequestTable",
						RequestMaxHandles: 0,
						HasResponse:       true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:        "uint32_t",
									LLDecl:      "uint32_t",
									IsPrimitive: true,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						ResponseSize:        20,
						ResponseTypeName:    "_TestSecondResponseTable",
						V1ResponseTypeName:  "v1__TestSecondResponseTable",
						ResponseMaxHandles:  0,
						CallbackType:        "SecondCallback",
						ResponseHandlerType: "Test_Second_ResponseHandler",
						ResponderType:       "Test_Second_Responder",
						LLProps: LLProps{
							InterfaceName:      "Test",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           52,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           52,
							},
						},
					},
				},
			},
		},
		{
			name: "Events",
			input: types.Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Name: types.EncodedCompoundIdentifier("EventTest"),
				Methods: []types.Method{
					{
						Ordinal:     1,
						GenOrdinal:  314159,
						Name:        types.Identifier("First"),
						HasResponse: true,
						Response: []types.Parameter{
							{
								Type: PrimitiveType(types.Int16),
								Name: types.Identifier("Value"),
							},
						},
						ResponseSize: 18,
					},
				},
			},
			expected: Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Namespace:           "::llcpp::",
				Name:                "EventTest",
				ClassName:           "EventTest_clazz",
				ServiceName:         "",
				ProxyName:           "EventTest_Proxy",
				StubName:            "EventTest_Stub",
				EventSenderName:     "EventTest_EventSender",
				SyncName:            "EventTest_Sync",
				SyncProxyName:       "EventTest_SyncProxy",
				RequestEncoderName:  "EventTest_RequestEncoder",
				RequestDecoderName:  "EventTest_RequestDecoder",
				ResponseEncoderName: "EventTest_ResponseEncoder",
				ResponseDecoderName: "EventTest_ResponseDecoder",
				HasEvents:           true,
				Methods: []Method{
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    1,
								GenOrdinal: 314159,
							},
							"kEventTest_First_Ordinal",
							"kEventTest_First_GenOrdinal",
						),
						Name:                 "First",
						NameInLowerSnakeCase: "first",
						Request:              []Parameter{},
						RequestSize:          0,
						RequestTypeName:      "_EventTestFirstRequestTable",
						V1RequestTypeName:    "v1__EventTestFirstRequestTable",
						RequestMaxHandles:    0,
						HasResponse:          true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:        "int16_t",
									LLDecl:      "int16_t",
									IsPrimitive: true,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						ResponseSize:        18,
						ResponseTypeName:    "_EventTestFirstEventTable",
						V1ResponseTypeName:  "v1__EventTestFirstEventTable",
						ResponseMaxHandles:  0,
						CallbackType:        "FirstCallback",
						ResponseHandlerType: "EventTest_First_ResponseHandler",
						ResponderType:       "EventTest_First_Responder",
						LLProps: LLProps{
							InterfaceName:      "EventTest",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: true,
								StackUse:           18,
							},
						},
					},
				},
			},
		},
		{
			name: "EventsTooBigForStack",
			input: types.Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Name: types.EncodedCompoundIdentifier("EventTest"),
				Methods: []types.Method{
					{
						Ordinal:     2,
						GenOrdinal:  271828,
						Name:        types.Identifier("Second"),
						HasResponse: true,
						Response: []types.Parameter{
							{
								Type: types.Type{
									Kind: types.ArrayType,
									ElementType: &types.Type{
										Kind:             types.PrimitiveType,
										PrimitiveSubtype: types.Int64,
									},
									ElementCount: addrOf(8000),
								},
								Name: types.Identifier("Value"),
							},
						},
						ResponseSize: 64016,
					},
				},
			},
			expected: Interface{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{Name: types.Identifier("ServiceName"), Value: "EventTest"},
					},
				},
				Namespace:           "::llcpp::",
				Name:                "EventTest",
				ClassName:           "EventTest_clazz",
				ServiceName:         "",
				ProxyName:           "EventTest_Proxy",
				StubName:            "EventTest_Stub",
				EventSenderName:     "EventTest_EventSender",
				SyncName:            "EventTest_Sync",
				SyncProxyName:       "EventTest_SyncProxy",
				RequestEncoderName:  "EventTest_RequestEncoder",
				RequestDecoderName:  "EventTest_RequestDecoder",
				ResponseEncoderName: "EventTest_ResponseEncoder",
				ResponseDecoderName: "EventTest_ResponseDecoder",
				HasEvents:           true,
				Methods: []Method{
					{
						Ordinals: types.NewOrdinalsStep7(
							types.Method{
								Ordinal:    2,
								GenOrdinal: 271828,
							},
							"kEventTest_Second_Ordinal",
							"kEventTest_Second_GenOrdinal",
						),
						Name:                 "Second",
						NameInLowerSnakeCase: "second",
						HasRequest:           false,
						Request:              []Parameter{},
						RequestSize:          0,
						RequestTypeName:      "_EventTestSecondRequestTable",
						V1RequestTypeName:    "v1__EventTestSecondRequestTable",
						RequestMaxHandles:    0,
						HasResponse:          true,
						Response: []Parameter{
							{
								Type: Type{
									Decl:        "::std::array<int64_t, 8000>",
									LLDecl:      "::fidl::Array<int64_t, 8000>",
									Dtor:        "~array",
									LLDtor:      "~Array",
									IsPrimitive: false,
								},
								Name:      "Value",
								OffsetOld: 0,
								OffsetV1:  0,
							},
						},
						ResponseSize:        64016,
						ResponseTypeName:    "_EventTestSecondEventTable",
						V1ResponseTypeName:  "v1__EventTestSecondEventTable",
						ResponseMaxHandles:  0,
						CallbackType:        "SecondCallback",
						ResponseHandlerType: "EventTest_Second_ResponseHandler",
						ResponderType:       "EventTest_Second_Responder",
						LLProps: LLProps{
							InterfaceName:      "EventTest",
							CBindingCompatible: true,
							LinearizeRequest:   false,
							LinearizeResponse:  false,
							ClientContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: false,
								StackUse:           0,
							},
							ServerContext: LLContextProps{
								StackAllocRequest:  true,
								StackAllocResponse: false,
								StackUse:           0,
							},
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
			result := CompileLL(root)
			actual, ok := result.Decls[0].(*Interface)
			if !ok || actual == nil {
				t.Fatalf("decls[0] not an interface, was instead %T", result.Decls[0])
			}
			if diff := cmp.Diff(ex.expected, *actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}

func TestCompileService(t *testing.T) {
	cases := []struct {
		name     string
		input    types.Service
		expected Service
	}{
		{
			name: "SingleMemberTest",
			input: types.Service{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("ServiceLevelAttribute"),
							Value: "TestValue",
						},
					},
				},
				Name: types.EncodedCompoundIdentifier("my.test/SingleMember"),
				Members: []types.ServiceMember{
					{
						Name: types.Identifier("first_member"),
						Type: types.Type{
							Kind:       types.IdentifierType,
							Identifier: types.EncodedCompoundIdentifier("my.test/Protocol"),
						},
					},
				},
			},
			expected: Service{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("ServiceLevelAttribute"),
							Value: "TestValue",
						},
					},
				},
				Namespace:   "::my::test",
				Name:        "SingleMember",
				ServiceName: "my.test.SingleMember",
				Members: []ServiceMember{
					{
						InterfaceType: "Protocol",
						Name:          "first_member",
						MethodName:    "first_member",
					},
				},
			},
		},
		{
			name: "MultiMemberTest",
			input: types.Service{
				Name: types.EncodedCompoundIdentifier("my.test/MultiMember"),
				Members: []types.ServiceMember{
					{
						Type: types.Type{
							Kind:       types.IdentifierType,
							Identifier: types.EncodedCompoundIdentifier("my.test/Protocol"),
						},
						Name: types.Identifier("first_member"),
					},
					{
						Type: types.Type{
							Kind:       types.IdentifierType,
							Identifier: types.EncodedCompoundIdentifier("lib/Protocol"),
						},
						Name: types.Identifier("second_member"),
					},
				},
			},
			expected: Service{
				Namespace:   "::my::test",
				Name:        "MultiMember",
				ServiceName: "my.test.MultiMember",
				Members: []ServiceMember{
					{
						InterfaceType: "Protocol",
						Name:          "first_member",
						MethodName:    "first_member",
					},
					{
						InterfaceType: "::lib::Protocol",
						Name:          "second_member",
						MethodName:    "second_member",
					},
				},
			},
		},
	}

	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				Name:      "my.test",
				Services:  []types.Service{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{ex.input.Name},
				Decls: types.DeclMap{
					types.EncodedCompoundIdentifier("my.test/Protocol"): types.InterfaceDeclType,
					types.EncodedCompoundIdentifier("lib/Protocol"):     types.InterfaceDeclType,
				},
			}
			result := Compile(root)
			actual, ok := result.Decls[0].(*Service)
			if !ok || actual == nil {
				t.Fatalf("decls[0] not a service, was instead %T", result.Decls[0])
			}
			if diff := cmp.Diff(ex.expected, *actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
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
				V1TableType:    "v1__TestTable",
				BiggestOrdinal: 2,
				MaxHandles:     0,
				Members: []TableMember{
					{
						Type: Type{
							Decl:        "int64_t",
							LLDecl:      "int64_t",
							IsPrimitive: true,
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
			if diff := cmp.Diff(ex.expected, *actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}

func TestCompileTableLlcppNamespaceShouldBeRenamed(t *testing.T) {
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
				Name: types.EncodedCompoundIdentifier("llcpp.foo/Test"),
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
				Namespace:      "::llcpp::llcpp_::foo",
				Name:           "Test",
				TableType:      "llcpp_foo_TestTable",
				V1TableType:    "v1_llcpp_foo_TestTable",
				BiggestOrdinal: 2,
				MaxHandles:     0,
				Members: []TableMember{
					{
						Type: Type{
							Decl:        "int64_t",
							LLDecl:      "int64_t",
							IsPrimitive: true,
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
				Name:   types.EncodedLibraryIdentifier("llcpp.foo"),
				Tables: []types.Table{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
			}
			result := CompileLL(root)
			actual, ok := result.Decls[0].(*Table)

			if !ok || actual == nil {
				t.Fatalf("decls[0] not an table, was instead %T", result.Decls[0])
			}
			if diff := cmp.Diff(ex.expected, *actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
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
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.XUnionMember{
					{
						Reserved: true,
						Ordinal:  0xbeefbabe,
					},
					{
						Reserved:   false,
						Attributes: types.Attributes{},
						Ordinal:    0xdeadbeef,
						Type: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Name:         types.Identifier("i"),
						Offset:       0,
						MaxOutOfLine: 0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
				Strictness:   types.IsFlexible,
			},
			expected: XUnion{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Namespace:   "::",
				Name:        "Test",
				TableType:   "_TestTable",
				V1TableType: "v1__TestTable",
				Members: []XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal:    0xdeadbeef,
						Type: Type{
							Decl:        "int64_t",
							LLDecl:      "int64_t",
							IsPrimitive: true,
						},
						Name:        "i",
						StorageName: "i_",
						TagName:     "kI",
						Offset:      0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
				Strictness:   types.IsFlexible,
			},
		},
		{
			name: "TwoArrays",
			input: types.XUnion{
				Attributes: types.Attributes{},
				Name:       types.EncodedCompoundIdentifier("Test"),
				Members: []types.XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal:    0x11111111,
						Type: types.Type{
							Kind: types.ArrayType,
							ElementType: &types.Type{
								Kind:             types.PrimitiveType,
								PrimitiveSubtype: types.Int64,
							},
							ElementCount: addrOf(10),
						},
						Name:         types.Identifier("i"),
						Offset:       0,
						MaxOutOfLine: 0,
					},
					{
						Attributes: types.Attributes{},
						Ordinal:    0x22222222,
						Type: types.Type{
							Kind: types.ArrayType,
							ElementType: &types.Type{
								Kind:             types.PrimitiveType,
								PrimitiveSubtype: types.Int64,
							},
							ElementCount: addrOf(20),
						},
						Name:         types.Identifier("j"),
						Offset:       0,
						MaxOutOfLine: 0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
				Strictness:   types.IsFlexible,
			},
			expected: XUnion{
				Attributes:  types.Attributes{},
				Namespace:   "::",
				Name:        "Test",
				TableType:   "_TestTable",
				V1TableType: "v1__TestTable",
				Members: []XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal:    0x11111111,
						Type: Type{
							Decl:        "::std::array<int64_t, 10>",
							LLDecl:      "::fidl::Array<int64_t, 10>",
							Dtor:        "~array",
							LLDtor:      "~Array",
							IsPrimitive: false,
						},
						Name:        "i",
						StorageName: "i_",
						TagName:     "kI",
						Offset:      0,
					},
					{
						Attributes: types.Attributes{},
						Ordinal:    0x22222222,
						Type: Type{
							Decl:        "::std::array<int64_t, 20>",
							LLDecl:      "::fidl::Array<int64_t, 20>",
							Dtor:        "~array",
							LLDtor:      "~Array",
							IsPrimitive: false,
						},
						Name:        "j",
						StorageName: "j_",
						TagName:     "kJ",
						Offset:      0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
				Strictness:   types.IsFlexible,
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
			if diff := cmp.Diff(ex.expected, *actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}

func TestCompileUnion(t *testing.T) {
	cases := []struct {
		name     string
		input    types.Union
		expected Union
	}{
		{
			name: "SingleInt64",
			input: types.Union{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.UnionMember{
					{
						Reserved:      true,
						XUnionOrdinal: 0xbeefbabe,
					},
					{
						Reserved:      false,
						Attributes:    types.Attributes{},
						XUnionOrdinal: 0xdeadbeef,
						Type: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Name:         types.Identifier("i"),
						Offset:       0,
						MaxOutOfLine: 0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
			},
			expected: Union{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Namespace:   "::",
				Name:        "Test",
				TableType:   "_TestTable",
				V1TableType: "v1__TestTable",
				Members: []UnionMember{
					{
						Attributes:    types.Attributes{},
						XUnionOrdinal: 0xdeadbeef,
						Type: Type{
							Decl:        "int64_t",
							LLDecl:      "int64_t",
							IsPrimitive: true,
						},
						Name:        "i",
						StorageName: "i_",
						TagName:     "kI",
						Offset:      0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				Unions: []types.Union{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
			}
			result := Compile(root)
			actual, ok := result.Decls[0].(*Union)

			if !ok || actual == nil {
				t.Fatalf("decls[0] not a union, was instead %T", result.Decls[0])
			}
			if diff := cmp.Diff(ex.expected, *actual); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}

func makeLiteralConstant(value string) types.Constant {
	return types.Constant{
		Kind: types.LiteralConstant,
		Literal: types.Literal{
			Kind:  types.NumericLiteral,
			Value: value,
		},
	}
}

func makePrimitiveType(subtype types.PrimitiveSubtype) types.Type {
	return types.Type{
		Kind:             types.PrimitiveType,
		PrimitiveSubtype: subtype,
	}
}

func TestCompileConstant(t *testing.T) {
	var c compiler
	cases := []struct {
		input    types.Constant
		typ      types.Type
		expected string
	}{
		{
			input:    makeLiteralConstant("10"),
			typ:      makePrimitiveType(types.Uint32),
			expected: "10u",
		},
		{
			input:    makeLiteralConstant("10"),
			typ:      makePrimitiveType(types.Float32),
			expected: "10",
		},
		{
			input:    makeLiteralConstant("-1"),
			typ:      makePrimitiveType(types.Int16),
			expected: "-1",
		},
		{
			input:    makeLiteralConstant("0xA"),
			typ:      makePrimitiveType(types.Uint32),
			expected: "0xA",
		},
		{
			input:    makeLiteralConstant("1.23"),
			typ:      makePrimitiveType(types.Float32),
			expected: "1.23",
		},
		{
			input:    makeLiteralConstant("-9223372036854775808"),
			typ:      makePrimitiveType(types.Int64),
			expected: "(-9223372036854775807ll-1)",
		},
		{
			input:    makeLiteralConstant("0x8000000000000000"),
			typ:      makePrimitiveType(types.Int64),
			expected: "(-9223372036854775807ll-1)",
		},
	}
	for _, ex := range cases {
		actual := c.compileConstant(ex.input, nil, ex.typ, "")
		if ex.expected != actual {
			t.Errorf("%v: expected %s, actual %s", ex.input, ex.expected, actual)
		}
	}
}
