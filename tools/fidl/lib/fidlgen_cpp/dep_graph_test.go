// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func expectNames(t *testing.T, decls []fidlgen.Decl, names []string) {
	if len(decls) != len(names) {
		t.Errorf("expected %d decls; got %d", len(names), len(decls))
	}
	for i, decl := range decls {
		if i >= len(names) {
			break
		}
		if string(decl.GetName()) != names[i] {
			t.Errorf("expected declaration name %s; got %s", names[i], decl.GetName())
		}
	}
}

func TestIndependentDeclsAreSourceOrdered(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Multiple([]string{
		`
		library example;

		const A int32 = 0;

		type B = enum : int8 {
			ZERO = 0;
		};

		const C bool = false;
	`,
		`
		library example;

		type D = struct {
			value uint64;
		};

		type E = bits : uint8 {
			ONE = 0b1;
		};
	`,
		`
		library example;

		alias F = bool;
	`,
	})
	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/A",
		"example/B",
		"example/C",
		"example/D",
		"example/E",
		"example/F",
	})
}

func TestInterdependentDeclsAreTopologicallySorted(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Multiple([]string{
		`
		library example;

		type A = enum : int32 {
			ZERO = B;
		};

		const B int32 = 0;
	`,
		`
		library example;

		alias C = vector<D>;

		type D = struct {
			value G;
			b E;
		};
	`,
		`
		library example;

		type E = bits : uint8 {
			ONE = 0b1;
		};

		alias F = D;

		alias G = uint64;
	`,
	})
	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/A",
		"example/E",
		"example/G",
		"example/D",
		"example/C",
		"example/F",
	})
}

func TestDirectDependency(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Multiple([]string{
		`
		library example;

		const A uint8 = B;
		const B uint8 = C;
		const C uint8 = 2;

		type D = enum : uint8 {
			MEMBER1 = B;
			MEMBER2 = 4;
		};
	`,
		`
		library example;

		type E = bits : uint8 {
			MEMBER = A;
		};

		const F D = D.MEMBER2;

		type G = struct {
			d D;
			e E;
		};
	`,
	})
	g := NewDeclDepGraph(ir)

	directDependents := func(t *testing.T, name string) []fidlgen.Decl {
		dependents, ok := g.GetDirectDependents(fidlgen.EncodedCompoundIdentifier(name))
		if !ok {
			t.Fatalf("unexpected declaration %s", name)
		}
		return dependents
	}

	expectNames(t, directDependents(t, "example/A"), []string{"example/E"})
	expectNames(t, directDependents(t, "example/B"), []string{"example/A", "example/D"})
	expectNames(t, directDependents(t, "example/C"), []string{"example/B"})
	expectNames(t, directDependents(t, "example/D"), []string{"example/F", "example/G"})
	expectNames(t, directDependents(t, "example/E"), []string{"example/G"})
	expectNames(t, directDependents(t, "example/G"), []string{})
}

func TestConstDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		const A C = B;

		const B C = C.MEMBER;

		type C = enum : uint8 {
			MEMBER = 0;
		};

	`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/C",
		"example/B",
		"example/A",
	})
}

func TestBitsDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		type A = bits : uint8 {
			ONE = B;
		};

		const B uint8 = 1;
	`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/A",
	})
}

func TestEnumDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		type A = enum : uint8 {
			ZERO = B;
		};

		const B uint8 = 0;
	`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/A",
	})
}

func TestResourceDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		resource_definition A : uint32 {
			properties {
				prop1 B;
				prop2 C;
			};
		};

		type B = enum : uint8 {
			ZERO = 0;
		};
		type C = bits : uint8 {
			ONE = 1;
		};
	`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/C",
		"example/A",
	})
}

func TestProtocolDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		protocol A {
			compose B;
		};

		protocol B {
			Foo() -> (D);

			Bar(C);
		};

		type C = struct {
			value uint64;
		};

		type D = resource struct{
			endpoint client_end:E;
		};

		protocol E {};
	`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/C",
		"example/D",
		"example/B",
		"example/A",
		"example/E", // Appears after D since no edges drawn to protocols specified via endpoints.
	})
}

func TestServiceDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		protocol A {};

		service B {
			first client_end:A;
			second client_end:C;
		};

		protocol C {};
	`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/A",
		"example/B", // No edges drawn to protocols specified via endpoints.
		"example/C",
	})
}

func TestStructDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
	library example;

	type A = struct {
		first uint64;
		second B;
		third C;
	};

	type B = struct {
		value bool;
	};

	alias C = D;

	type D = enum : uint8 {
		MEMBER = 0;
	};
`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/D",
		"example/C",
		"example/A",
	})
}

func TestTableDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
	library example;

	type A = table {
		1: first uint64;
		2: second B;
		3: third C;
	};

	type B = struct {
		value bool;
	};

	alias C = D;

	type D = enum : uint8 {
		MEMBER = 0;
	};
`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/D",
		"example/C",
		"example/A",
	})
}

func TestUnionDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
	library example;

	type A = union {
		1: first uint64;
		2: second B;
		3: third C;
	};

	type B = struct {
		value bool;
	};

	alias C = D;

	type D = enum : uint8 {
		MEMBER = 0;
	};
`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/D",
		"example/C",
		"example/A",
	})
}

// TODO(fxbug.dev/105758): Test aliases of aliases too.
func TestAliasDeps(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
	library example;

	alias A = B;

	type B = struct {
		value bool;
	};
`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/A",
	})
}

func TestNewTypeDeps(t *testing.T) {
	t.Skip("TODO(fxbug.dev/7807): Support new types")

	ir := fidlgentest.EndToEndTest{T: t}.Single(`
	library example;

	type A = B;

	alias B = bool;

	type C = D;

	type D = struct {
		value bool;
	};
`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/B",
		"example/A",
		"example/D",
		"example/C",
	})
}

func TestNoEdgesToNullableTypes(t *testing.T) {
	ir := fidlgentest.EndToEndTest{T: t}.Single(`
	library example;

	type A = struct {
		nullable B:optional;
	};

	type B = struct {
		value uint64;
	};

	type C = resource struct {
		nullable server_end:<D, optional>;
	};

	protocol D {};
`)

	g := NewDeclDepGraph(ir)
	expectNames(t, g.SortedDecls(), []string{
		"example/A",
		"example/B",
		"example/C",
		"example/D",
	})
}
