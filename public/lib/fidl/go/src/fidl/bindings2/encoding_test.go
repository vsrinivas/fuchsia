// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"reflect"
	"testing"
)

func testIdentity(t *testing.T, input Payload, expectSize int, output Payload) {
	m := MessageHeader{
		Txid:     1215,
		Reserved: 0,
		Flags:    111111,
		Ordinal:  889,
	}
	bytes, handles, err := Marshal(&m, input)
	if err != nil {
		t.Fatal(err)
	}
	totalSize := expectSize + MessageHeaderSize
	if len(bytes) != totalSize {
		t.Fatalf("expected size %d but got %d: %v", totalSize, len(bytes), bytes)
	}
	h, err := Unmarshal(bytes, handles, output)
	if err != nil {
		t.Fatal(err)
	}
	if *h != m {
		t.Fatal("expected header: %v, got %v", m, h)
	}
	if !reflect.DeepEqual(input, output) {
		t.Fatalf("expected: %v, got %v", input, output)
	}
}

func TestEncodingIdentity(t *testing.T) {
	t.Run("Simple", func(t *testing.T) {
		testIdentity(t, &TestSimple{X: 124}, 8, &TestSimple{})
		testIdentity(t, &TestSimpleBool{X: true}, 8, &TestSimpleBool{})
	})
	t.Run("Alignment", func(t *testing.T) {
		testIdentity(t, &TestAlignment1{X: -36, Y: -10, Z: 51}, 8, &TestAlignment1{})
		testIdentity(t, &TestAlignment2{
			A: 1212141,
			B: 908935,
			C: -1,
			D: 125,
			E: -22,
			F: 111,
			G: 1515,
			H: 65535,
			I: 1515,
		}, 24, &TestAlignment2{})
	})
	t.Run("Floats", func(t *testing.T) {
		testIdentity(t, &TestFloat1{A: -36.0}, 8, &TestFloat1{})
		testIdentity(t, &TestFloat2{A: -1254918271.0}, 8, &TestFloat2{})
		testIdentity(t, &TestFloat3{A: 1241.1, B: 0.2141, C: 20, D: 0.0}, 32, &TestFloat3{})
	})
	t.Run("Arrays", func(t *testing.T) {
		testIdentity(t, &TestArray1{A: [5]int8{1, 77, 2, 4, 5}}, 8, &TestArray1{})
		testIdentity(t, &TestArray2{A: -1.0, B: [1]float32{0.2}}, 16, &TestArray2{})
		testIdentity(t, &TestArray3{
			A: -999,
			B: [3]uint16{11, 12, 13},
			C: 1021,
		}, 24, &TestArray3{})
		testIdentity(t, &TestArray4{
			A: [9]bool{true, false, false, true, false, true, true, true, true},
		}, 16, &TestArray4{})
	})
}
