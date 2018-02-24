// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"reflect"
	"testing"
)

func testIdentity(t *testing.T, input interface{}, expectSize int, output interface{}) {
	m := MessageHeader{
		Txid: 1215,
		Reserved: 0,
		Flags: 111111,
		Ordinal: 889,
	}
	bytes, handles, err := Marshal(&m, input)
	if err != nil {
		t.Fatal(err)
	}
	if len(bytes) != (expectSize + MessageHeaderSize) {
		t.Fatalf("expected size %d but got %d: %v", expectSize, len(bytes), bytes)
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
		type test struct {
			I uint64
		}
		testIdentity(t, &test{I: 124}, 8, &test{})
		type testBool struct {
			X bool
		}
		testIdentity(t, &testBool{X: true}, 8, &testBool{})
	})
	t.Run("Alignment", func(t *testing.T) {
		type test struct {
			X int8
			Y int8
			I uint32
		}
		testIdentity(t, &test{X: -36, Y: -10, I: 51}, 8, &test{})
		type testPack struct {
			Y uint32
			Z uint32
			A int8
			B int8
			C int8
			D uint8
			E uint32
			F uint16
			G uint16
		}
		testIdentity(t, &testPack{
			Y: 1212141,
			Z: 908935,
			A: -1,
			B: 125,
			C: -22,
			D: 111,
			E: 1515,
			F: 65535,
			G: 1515,
		}, 24, &testPack{})
	})
	t.Run("Floats", func(t *testing.T) {
		type test struct {
			X float32
		}
		testIdentity(t, &test{X: -36.0}, 8, &test{})
		type test2 struct {
			X float64
		}
		testIdentity(t, &test2{X: -1254918271.0}, 8, &test2{})
		type test3 struct {
			X float32
			Y float64
			I uint64
			Z float32
		}
		testIdentity(t, &test3{X: 1241.1, Y: 0.2141, I: 20, Z: 0.0}, 32, &test3{})
	})
	t.Run("Arrays", func(t *testing.T) {
		type test struct {
			X [5]int8
		}
		testIdentity(t, &test{X: [5]int8{1, 77, 2, 4, 5}}, 8, &test{})
		type test2 struct {
			X float64
			Y [1]float32
		}
		testIdentity(t, &test2{X: -1.0, Y: [1]float32{0.2}}, 16, &test2{})
		type test3 struct {
			X int32
			Y [3]uint16
			I uint64
		}
		testIdentity(t, &test3{X: -999, Y: [3]uint16{11, 12, 13}, I: 1021}, 24, &test3{})
		type test4 struct {
			B [9]bool
		}
		testIdentity(t, &test4{
			B: [9]bool{true, false, false, true, false, true, true, true, true},
		}, 16, &test4{})
	})
}
