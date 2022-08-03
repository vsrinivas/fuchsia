// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgentest

import (
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestEndToEndExample(t *testing.T) {
	root := EndToEndTest{T: t}.Single(`library example;

	type MyStruct = struct {
		field1 string;
		field2 string;
	};`)

	if root.Name != "example" {
		t.Errorf("expected 'example', was '%s'", root.Name)
	}
}

func TestHandleObjType(t *testing.T) {
	root := EndToEndTest{T: t}.Single(`library example;

	type obj_type = enum : uint32 {
		NONE = 0;
		VMO = 3;
	};

	resource_definition handle : uint32 {
		properties {
			subtype obj_type;
		};
	};

	type MyStruct = resource struct {
		field handle:VMO;
	};`)

	if root.Structs[0].Members[0].Type.ObjType != 3 {
		t.Errorf("expected '3', was '%d'", root.Structs[0].Members[0].Type.ObjType)
	}
}

func TestErrorSyntaxOfImportedComposedProtocol(t *testing.T) {
	root := EndToEndTest{T: t}.WithDependency(`library parent;

	protocol Parent {
		Method() -> (struct{}) error uint32;
	};

`).Single(`library child;

	using parent;

	protocol Child {
		compose parent.Parent;
	};

`)

	if len(root.Protocols) != 1 {
		t.Fatalf("expected one protocol, found %v", root.Protocols)
	}
	if len(root.Protocols[0].Methods) != 1 {
		t.Fatalf("expected one method, found %v", root.Protocols[0].Methods)
	}
	method := root.Protocols[0].Methods[0]

	if !method.HasError {
		t.Fatalf("expected method to have an error syntax")
	}
	if method.ResultType == nil {
		t.Fatalf("expected a .ResultType")
	}
	if method.ValueType == nil {
		t.Fatalf("expected a .ValueType")
	}
	if method.ErrorType == nil {
		t.Fatalf("expected a .ErrorType")
	}
}

func TestMultipleFiles(t *testing.T) {
	root := EndToEndTest{T: t}.WithDependency(`library dependency;

	protocol Foo {
		Method() -> (struct{}) error uint32;
	};

`).Multiple([]string{
		`
	library example;

	const A bool = true;

	type B = enum : int8 {
		ZERO = 0;
		ONE = 1;
	};
`, `
	library example;

	type C = bits : uint8 {
		FIRST = 0b00000001;
		LAST = 0b10000000;
	};

	const D uint16 = 0xffff;

	type E = struct {
		x uint64;
		y uint64;
		b B;
	};
`, `
	library example;

	using dependency;

	protocol F {
		compose dependency.Foo;
	};
`,
	})

	if len(root.Consts) == 2 {
		constNames := []string{string(root.Consts[0].Name), string(root.Consts[1].Name)}
		sort.Strings(constNames)

		if diff := cmp.Diff(constNames, []string{"example/A", "example/D"}); diff != "" {
			t.Error(diff)
		}
	} else {
		t.Fatalf("incorrect number of consts (%d): %#v", len(root.Consts), root.Consts)
	}

	if len(root.Enums) == 1 {
		if name := string(root.Enums[0].Name); name != "example/B" {
			t.Errorf("incorrect enum name: %s", name)
		}
	} else {
		t.Errorf("incorrect number of enums (%d): %#v", len(root.Enums), root.Enums)
	}

	if len(root.Bits) == 1 {
		if name := string(root.Bits[0].Name); name != "example/C" {
			t.Errorf("incorrect bits name: %s", name)
		}
	} else {
		t.Errorf("incorrect number of bits (%d): %#v", len(root.Bits), root.Bits)
	}

	if len(root.Structs) == 1 {
		if name := string(root.Structs[0].Name); name != "example/E" {
			t.Errorf("incorrect struct name: %s", name)
		}
	} else {
		t.Errorf("incorrect number of structs (%d): %#v", len(root.Structs), root.Structs)
	}

	if len(root.Protocols) == 1 {
		if name := string(root.Protocols[0].Name); name != "example/F" {
			t.Errorf("incorrect protocol name: %s", name)
		}
	} else {
		t.Errorf("incorrect number of protocols (%d): %#v", len(root.Protocols), root.Protocols)
	}
}
