// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgentest

import (
	"testing"
)

func TestEndToEndExample(t *testing.T) {
	root := EndToEndTest{T: t}.Single(`library example;

	struct MyStruct {
		string field1;
		string field2;
	};`)

	if root.Name != "example" {
		t.Errorf("expected 'example', was '%s'", root.Name)
	}
}

func TestHandleObjType(t *testing.T) {
	root := EndToEndTest{T: t}.Single(`library example;

	enum obj_type : uint32 {
		NONE = 0;
		VMO = 3;
	};

	resource_definition handle : uint32 {
		properties {
			obj_type subtype;
		};
	};

	resource struct MyStruct {
		handle:VMO field;
	};`)

	if root.Structs[0].Members[0].Type.ObjType != 3 {
		t.Errorf("expected '3', was '%d'", root.Structs[0].Members[0].Type.ObjType)
	}
}
