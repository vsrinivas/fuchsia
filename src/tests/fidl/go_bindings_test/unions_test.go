// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package go_bindings_test

import (
	"bytes"
	"syscall/zx/fidl"
	"testing"

	"fidl/fidl/go/types"

	"github.com/google/go-cmp/cmp"
)

func TestUnionWithUnknownData(t *testing.T) {
	union := types.ExampleFlexibleUnion{
		I_exampleFlexibleUnionTag: 3, // a reserved ordinal
		I_unknownData: fidl.UnknownData{
			Bytes: []byte{51},
		},
	}
	if union.Which() != types.ExampleFlexibleUnion_unknownData {
		t.Errorf("Expected unknown union to have unknown tag")
	}

	data := union.GetUnknownData()
	if want := []byte{51}; !bytes.Equal(data.Bytes, want) {
		t.Errorf("Expected unknown data to be %s, got: %s", want, data.Bytes)
	}
	if len(data.Handles) != 0 {
		t.Errorf("Expected empty unknown handles, got %d", data.Handles)
	}

	union.SetD(42.42)
	if diff := cmp.Diff(union, types.ExampleFlexibleUnionWithD(42.42)); diff != "" {
		t.Errorf("Expected modified-in-place union modified from unknown variant and constructed union to be equal: %s", diff)
	}

	union.SetA([3]int8{0, 1, 2})
	if diff := cmp.Diff(union, types.ExampleFlexibleUnionWithA([3]int8{0, 1, 2})); diff != "" {
		t.Errorf("Expected modified-in-place union modified from known variant and constructed union to be equal: %s", diff)
	}
}

func TestStrictUnion(t *testing.T) {
	var val types.ExampleStrictUnion
	// without a selected variant
	if actual := val.Which(); actual != 0 {
		t.Fatalf("actual='%d', expected='%d'", actual, 0)
	}
	if actual := val.Ordinal(); actual != 0 {
		t.Fatalf("actual='%d', expected='%d'", actual, 0)
	}

	// with a selected variant
	val.SetFoo("hello")
	if actual := val.Which(); actual != types.ExampleStrictUnionFoo {
		t.Fatalf("actual='%d', expected='%d'", actual, types.ExampleStrictUnionFoo)
	}
	if actual := val.Ordinal(); actual != 1 {
		t.Fatalf("actual='%d', expected='%d'", actual, 1)
	}
}

func TestFlexibleUnion(t *testing.T) {
	var val types.ExampleFlexibleUnion
	// without a selected variant
	if actual := val.Which(); actual != types.ExampleFlexibleUnion_unknownData {
		t.Fatalf("actual='%d', expected='%d'", actual, types.ExampleFlexibleUnion_unknownData)
	}
	if actual := val.Ordinal(); actual != 0 {
		t.Fatalf("actual='%d', expected='%d'", actual, 0)
	}

	// with a selected variant
	val.SetFoo("hello")
	if actual := val.Which(); actual != types.ExampleFlexibleUnionFoo {
		t.Fatalf("actual='%d', expected='%d'", actual, types.ExampleFlexibleUnionFoo)
	}
	if actual := val.Ordinal(); actual != 4 {
		t.Fatalf("actual='%d', expected='%d'", actual, 4)
	}
}
