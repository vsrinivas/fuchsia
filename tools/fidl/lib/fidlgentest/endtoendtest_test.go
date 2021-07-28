// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgentest

import (
	"testing"
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
