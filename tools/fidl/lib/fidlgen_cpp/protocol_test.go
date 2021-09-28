// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"sync"
	"testing"

	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func onlyProtocol(t *testing.T, root Root) Protocol {
	var protocols []Protocol
	for _, decl := range root.Decls {
		if p, ok := decl.(Protocol); ok {
			protocols = append(protocols, p)
		}
	}
	if len(protocols) != 1 {
		t.Fatal("Must have a single protocol defined")
	}
	return protocols[0]
}

var exampleProtocol = func() func(t *testing.T) Protocol {
	var once sync.Once
	var p Protocol
	return func(t *testing.T) Protocol {
		once.Do(func() {
			root := compile(fidlgentest.EndToEndTest{T: t}.Single(`
library example;

protocol P {
	OneWay();
	TwoWay() -> ();
	-> Event();
};
`))
			p = onlyProtocol(t, root)
		})
		return p
	}
}()

func toNames(methods []*Method) []string {
	var s []string
	for _, m := range methods {
		s = append(s, m.Natural.Name())
	}
	return s
}

func TestMatchOneWayMethods(t *testing.T) {
	expectEqual(t, toNames(exampleProtocol(t).OneWayMethods), []string{"OneWay"})
}

func TestMatchTwoWayMethods(t *testing.T) {
	expectEqual(t, toNames(exampleProtocol(t).TwoWayMethods), []string{"TwoWay"})
}

func TestMatchClientMethods(t *testing.T) {
	expectEqual(t, toNames(exampleProtocol(t).ClientMethods), []string{"OneWay", "TwoWay"})
}

func TestMatchEvents(t *testing.T) {
	expectEqual(t, toNames(exampleProtocol(t).Events), []string{"Event"})
}

func TestFullyQualifiedName(t *testing.T) {
	expectEqual(t, exampleProtocol(t).OneWayMethods[0].FullyQualifiedName, "example/P.OneWay")
}

//
// Test allocation strategies
//

func TestWireBindingsAllocation(t *testing.T) {
	cases := []struct {
		desc          string
		fidl          string
		actualChooser func(p Protocol) allocation
		expected      allocation
		expectedType  string
	}{
		{
			desc:          "client request inlined",
			fidl:          "protocol P { Method(struct { a array<uint8, 496>; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Request.ClientAllocationV1 },
			expected: allocation{
				IsStack: true,
				Size:    512,
			},
			expectedType: "::fidl::internal::InlineMessageBuffer<512>",
		},
		{
			desc:          "client request boxed due to message size",
			fidl:          "protocol P { Method(struct { a array<uint8, 497>; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Request.ClientAllocationV1 },
			expected: allocation{
				IsStack: false,
				Size:    0,
			},
			expectedType: "::fidl::internal::BoxedMessageBuffer<520>",
		},
		{
			desc: "client request inlined despite message flexibility",
			fidl: "type Flexible = flexible union { 1: a int32; };" +
				"protocol P { Method(struct { a Flexible; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Request.ClientAllocationV1 },
			expected: allocation{
				IsStack: true,
				Size:    48,
			},
			expectedType: "::fidl::internal::InlineMessageBuffer<48>",
		},
		{
			desc: "client response boxed due to message flexibility",
			fidl: "type Flexible = flexible union { 1: a int32; };" +
				"protocol P { Method() -> (struct { a Flexible; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Response.ClientAllocationV1 },
			expected: allocation{
				IsStack: false,
				Size:    0,
			},
			expectedType: "::fidl::internal::BoxedMessageBuffer<ZX_CHANNEL_MAX_MSG_BYTES>",
		},
		{
			desc:          "server response inlined",
			fidl:          "protocol P { Method() -> (struct { a array<uint8, 496>; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Response.ServerAllocationV1 },
			expected: allocation{
				IsStack: true,
				Size:    512,
			},
			expectedType: "::fidl::internal::InlineMessageBuffer<512>",
		},
		{
			desc:          "server response boxed due to message size",
			fidl:          "protocol P { Method() -> (struct { a array<uint8, 497>; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Response.ServerAllocationV1 },
			expected: allocation{
				IsStack: false,
				Size:    0,
			},
			expectedType: "::fidl::internal::BoxedMessageBuffer<520>",
		},
		{
			desc: "server response inlined despite message flexibility",
			fidl: "type Flexible = flexible union { 1: a int32; };" +
				"protocol P { Method() -> (struct { a Flexible; }); };",
			actualChooser: func(p Protocol) allocation { return p.Methods[0].Response.ServerAllocationV1 },
			expected: allocation{
				IsStack: true,
				Size:    48,
			},
			expectedType: "::fidl::internal::InlineMessageBuffer<48>",
		},
		{
			desc: "client sync event handling inlined",
			fidl: "protocol P {" +
				"    -> Event1(struct { a int32; });" +
				"    -> Event2(struct { a int32; b int32; });" +
				"};",
			actualChooser: func(p Protocol) allocation { return p.SyncEventAllocationV1 },
			expected: allocation{
				IsStack: true,
				Size:    24,
			},
			expectedType: "::fidl::internal::InlineMessageBuffer<24>",
		},
		{
			desc: "client sync event handling boxed due to message size",
			fidl: "protocol P {" +
				"    -> Event1(struct { a array<uint8, 497>; });" +
				"    -> Event2(struct { a int32; b int32; });" +
				"};",
			actualChooser: func(p Protocol) allocation { return p.SyncEventAllocationV1 },
			expected: allocation{
				IsStack: false,
				Size:    0,
			},
			expectedType: "::fidl::internal::BoxedMessageBuffer<520>",
		},
		{
			desc: "client sync event handling boxed due to message flexibility",
			fidl: "type Flexible = table {};" +
				"protocol P {" +
				"    -> Event1(struct { f Flexible; });" +
				"    -> Event2(struct { a int32; b int32; });" +
				"};",
			actualChooser: func(p Protocol) allocation { return p.SyncEventAllocationV1 },
			expected: allocation{
				IsStack: false,
				Size:    0,
			},
			expectedType: "::fidl::internal::BoxedMessageBuffer<ZX_CHANNEL_MAX_MSG_BYTES>",
		},
		{
			desc: "client sync event handling inlined ignoring flexible two-way response",
			fidl: "type Flexible = table {};" +
				"protocol P {" +
				"    Method() -> (struct { f Flexible; });" +
				"    -> Event2(struct { a int32; b int32; });" +
				"};",
			actualChooser: func(p Protocol) allocation { return p.SyncEventAllocationV1 },
			expected: allocation{
				IsStack: true,
				Size:    32,
			},
			expectedType: "::fidl::internal::InlineMessageBuffer<32>",
		},
	}
	for _, ex := range cases {
		t.Run(ex.desc, func(t *testing.T) {
			root := compile(fidlgentest.EndToEndTest{T: t}.Single("library example; " + ex.fidl))
			var protocols []Protocol
			for _, decl := range root.Decls {
				if p, ok := decl.(Protocol); ok {
					protocols = append(protocols, p)
				}
			}
			if len(protocols) != 1 {
				t.Fatal("Must have a single protocol defined")
			}

			p := protocols[0]
			actual := ex.actualChooser(p)
			expectEqual(t, actual, ex.expected, cmpopts.IgnoreUnexported(allocation{}))
			expectEqual(t, actual.BackingBufferType(), ex.expectedType)
		})
	}
}

//
// Test protocol associated declaration names
//

func TestHlMessagingProtocolAssociatedNames(t *testing.T) {
	fidl := `
library fuchsia.foobar;

// Regular protocol
protocol P {};
`
	root := compile(fidlgentest.EndToEndTest{T: t}.Single(fidl))

	messaging := root.Decls[0].(Protocol).hlMessaging
	assertEqual(t, messaging.ProtocolMarker.String(), "::fuchsia::foobar::P")
	assertEqual(t, messaging.InterfaceAliasForStub.String(), "::fuchsia::foobar::P_Stub::P_clazz")
	assertEqual(t, messaging.Proxy.String(), "::fuchsia::foobar::P_Proxy")
	assertEqual(t, messaging.Stub.String(), "::fuchsia::foobar::P_Stub")
	assertEqual(t, messaging.EventSender.String(), "::fuchsia::foobar::P_EventSender")
	assertEqual(t, messaging.SyncInterface.String(), "::fuchsia::foobar::P_Sync")
	assertEqual(t, messaging.SyncProxy.String(), "::fuchsia::foobar::P_SyncProxy")
	assertEqual(t, messaging.RequestEncoder.String(), "::fuchsia::foobar::P_RequestEncoder")
	assertEqual(t, messaging.RequestDecoder.String(), "::fuchsia::foobar::P_RequestDecoder")
	assertEqual(t, messaging.ResponseEncoder.String(), "::fuchsia::foobar::P_ResponseEncoder")
	assertEqual(t, messaging.ResponseDecoder.String(), "::fuchsia::foobar::P_ResponseDecoder")
}

func TestWireMessagingProtocolAssociatedNames(t *testing.T) {
	fidl := `
library fuchsia.foobar;

// Regular protocol
protocol P {};
`
	root := compile(fidlgentest.EndToEndTest{T: t}.Single(fidl))

	messaging := root.Decls[0].(Protocol).wireTypeNames
	assertEqual(t, messaging.WireProtocolMarker.String(), "::fuchsia_foobar::P")
	assertEqual(t, messaging.WireSyncClient.String(), "::fidl::WireSyncClient<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireClient.String(), "::fidl::WireClient<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireSyncEventHandler.String(), "::fidl::WireSyncEventHandler<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireAsyncEventHandler.String(), "::fidl::WireAsyncEventHandler<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireServer.String(), "::fidl::WireServer<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireEventSender.String(), "::fidl::WireEventSender<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireWeakEventSender.String(), "::fidl::internal::WireWeakEventSender<::fuchsia_foobar::P>")
	assertEqual(t, messaging.WireClientImpl.String(), "::fidl::internal::WireClientImpl<::fuchsia_foobar::P>")
}
