// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func TestCompileTypeNames(t *testing.T) {
	root := compile(fidlgentest.EndToEndTest{T: t}.Single(`
library foo.bar;

type U = union {
	1: a uint32;
};

protocol P {
	M(resource struct { a array<U:optional, 3>; b vector<client_end:<P, optional>>; });
};
`))
	p := onlyProtocol(t, root)
	assertEqual(t, len(p.Methods), 1)
	m := p.Methods[0]
	assertEqual(t, len(m.RequestArgs), 2)

	ty := m.RequestArgs[0].Type
	expectEqual(t, ty.HLCPP.String(), "::std::array<::std::unique_ptr<::foo::bar::U>, 3>")
	expectEqual(t, ty.Wire.String(), "::fidl::Array<::fidl::WireOptional<::foo_bar::wire::U>, 3>")
	expectEqual(t, ty.Unified.String(), "::std::array<::std::unique_ptr<::foo_bar::U>, 3>")

	ty = m.RequestArgs[1].Type
	expectEqual(t, ty.HLCPP.String(), "::std::vector<::fidl::InterfaceHandle<::foo::bar::P>>")
	expectEqual(t, ty.Wire.String(), "::fidl::VectorView<::fidl::ClientEnd<::foo_bar::P>>")
	expectEqual(t, ty.Unified.String(), "::std::vector<::fidl::ClientEnd<::foo_bar::P>>")
}

func makeTestName(s string) nameVariants {
	return nameVariants{
		HLCPP:   makeName("example::" + s),
		Unified: makeName("example::" + s),
		Wire:    makeName("example::wire::" + s),
	}
}

func TestAnonymousLayoutAliases(t *testing.T) {
	currentVariant = wireVariant

	cases := []struct {
		desc     string
		fidl     string
		expected map[namingContextKey][]ScopedLayout
	}{
		{
			desc: "single struct",
			fidl: "type Foo = struct { bar struct {}; };",
			expected: map[namingContextKey][]ScopedLayout{
				"Foo": {{
					scopedName:    stringNamePart("Bar"),
					flattenedName: makeTestName("Bar"),
				}},
			},
		},
		{
			desc: "request",
			fidl: `
		protocol MyProtocol {
			MyMethod(struct { req_data struct {}; });
		};
		`,
			expected: map[namingContextKey][]ScopedLayout{
				"MyMethodRequest (with header)": {{
					scopedName:    stringNamePart("ReqData"),
					flattenedName: makeTestName("ReqData"),
				}},
				"MyProtocolMyMethodRequest": {{
					scopedName:    stringNamePart("ReqData"),
					flattenedName: makeTestName("ReqData"),
				}},
			},
		},
		{
			desc: "result",
			fidl: `
		protocol MyProtocol {
			MyMethod() -> (struct {}) error enum : uint32 { FOO = 1; };
		};
		`,
			expected: map[namingContextKey][]ScopedLayout{
				"MyMethodResponse (with header)": {{
					scopedName:    stringNamePart("Result"),
					flattenedName: makeTestName("MyProtocolMyMethodResult"),
				}},
				"MyProtocolMyMethodTopResponse": {{
					scopedName:    stringNamePart("Result"),
					flattenedName: makeTestName("MyProtocolMyMethodResult"),
				}},
				"MyProtocolMyMethodResult": {
					{
						scopedName:    stringNamePart("Err"),
						flattenedName: makeTestName("MyProtocolMyMethodError"),
					},
					{
						scopedName:    stringNamePart("Response"),
						flattenedName: makeTestName("MyProtocolMyMethodResponse"),
					},
				},
			},
		},
		{
			desc: "expression",
			fidl: `
		type Expression = flexible union {
			1: value uint64;
			2: bin_op struct {
				op flexible enum {
					ADD = 1;
					SUB = 2;
				};
				left Expression:optional;
				right Expression:optional;
			};
			3: flags bits {
				FLAGS_A = 1;
			};
		};
		`,
			expected: map[namingContextKey][]ScopedLayout{
				"Expression": {
					{
						scopedName:    stringNamePart("Flags"),
						flattenedName: makeTestName("Flags"),
					},
					{
						scopedName:    stringNamePart("BinOp"),
						flattenedName: makeTestName("BinOp"),
					},
				},
				"BinOp": {{
					scopedName:    stringNamePart("Op"),
					flattenedName: makeTestName("Op"),
				}},
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.desc, func(t *testing.T) {
			root := compile(fidlgentest.EndToEndTest{T: t}.Single("library example; " + ex.fidl))

			layoutToChildren := make(map[namingContextKey][]ScopedLayout)
			for _, decl := range root.Decls {
				switch d := decl.(type) {
				case *Struct:
					if len(d.AnonymousChildren) > 0 {
						layoutToChildren[d.Name()] = d.AnonymousChildren
					}
				case *Union:
					if len(d.AnonymousChildren) > 0 {
						layoutToChildren[d.Name()] = d.AnonymousChildren
					}
				case *Table:
					if len(d.AnonymousChildren) > 0 {
						layoutToChildren[d.Name()] = d.AnonymousChildren
					}
				case *Protocol:
					for _, m := range d.Methods {
						if len(m.RequestAnonymousChildren) > 0 {
							layoutToChildren[m.Name()+"Request (with header)"] = m.RequestAnonymousChildren
						}
						if len(m.ResponseAnonymousChildren) > 0 {
							layoutToChildren[m.Name()+"Response (with header)"] = m.ResponseAnonymousChildren
						}
					}
				}
			}

			comparer := cmp.Comparer(func(x, y ScopedLayout) bool {
				return x.ScopedName() == y.ScopedName() && x.FlattenedName() == y.FlattenedName()
			})
			if !cmp.Equal(layoutToChildren, ex.expected, comparer) {
				t.Error(cmp.Diff(layoutToChildren, ex.expected, comparer))
			}
		})
	}
}
