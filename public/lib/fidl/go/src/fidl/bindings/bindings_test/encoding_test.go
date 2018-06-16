// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"reflect"
	"syscall/zx"
	"testing"

	. "fidl/bindings"
	. "fidl/fidl/test/gobindings"
)

func testIdentity(t *testing.T, input Payload, expectSize int, output Payload) {
	var respb [zx.ChannelMaxMessageBytes]byte
	var resph [zx.ChannelMaxMessageHandles]zx.Handle
	nb, nh, err := Marshal(input, respb[:], resph[:])
	if err != nil {
		t.Fatal(err)
	}
	if nb != expectSize {
		t.Fatalf("expected size %d but got %d: %v", expectSize, nb, respb[:nb])
	}
	if err := Unmarshal(respb[:nb], resph[:nh], output); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(input, output) {
		t.Fatalf("expected: %v, got: %v", input, output)
	}
}

func TestEncodingIdentity(t *testing.T) {
	t.Run("Header", func(t *testing.T) {
		var buf [MessageHeaderSize]byte
		m := MessageHeader{
			Txid:     1215,
			Reserved: 0,
			Flags:    111111,
			Ordinal:  889,
		}
		MarshalHeader(&m, buf[:])

		var header MessageHeader
		if err := UnmarshalHeader(buf[:], &header); err != nil {
			t.Fatal(err)
		}
		if m != header {
			t.Fatalf("expected: %v, got: %v", m, header)
		}
	})
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
	t.Run("Strings", func(t *testing.T) {
		testIdentity(t, &TestString1{A: "str", B: nil}, 40, &TestString1{})
		testIdentity(t, &TestString2{A: [2]string{"hello", "g"}}, 48, &TestString2{})
		s := "bye"
		testIdentity(t, &TestString3{
			A: [2]string{"boop", "g"},
			B: [2]*string{&s, nil},
		}, 88, &TestString3{})
	})
	t.Run("Vectors", func(t *testing.T) {
		v1 := []int64{-1}
		testIdentity(t, &TestVector1{
			A: []int8{1, 2, 3, 4},
			B: nil,
			C: []int32{99},
			D: &v1,
		}, 88, &TestVector1{})
		v2 := []string{"x", "hello"}
		testIdentity(t, &TestVector2{
			A: [2][]int8{{9, -1}, {}},
			B: [][]int8{{-111, 41}, {-1, -1, -1, -1}},
			C: []*[]string{nil, &v2},
		}, 200, &TestVector2{})
	})
	t.Run("Structs", func(t *testing.T) {
		testIdentity(t, &TestStruct1{
			A: TestSimple{
				X: -9999,
			},
			B: &TestSimple{
				X: 1254125,
			},
		}, 24, &TestStruct1{})
		v1 := []int64{101010}
		testIdentity(t, &TestStruct2{
			A: TestArray1{
				A: [5]int8{1, 77, 2, 4, 5},
			},
			B: TestFloat1{
				A: 2.81212,
			},
			C: TestVector1{
				A: []int8{1, 2, 3, 4},
				B: nil,
				C: []int32{99},
				D: &v1,
			},
			D: &TestString1{
				A: "str",
				B: nil,
			},
		}, 152, &TestStruct2{})
	})
	t.Run("Unions", func(t *testing.T) {
		u1 := Union1{}
		u1.SetB(TestSimple{X: 555})
		testIdentity(t, &TestUnion1{
			A: u1,
			B: nil,
		}, 24, &TestUnion1{})
		testIdentity(t, &TestUnion2{
			A: []Union1{u1, u1, u1},
			B: []*Union1{&u1, nil, nil},
		}, 120, &TestUnion2{})
		u2 := Union2{}
		u2.SetA([4]int8{1, 77, 2, 4})
		testIdentity(t, &TestUnion3{
			A: u2,
			B: 1842,
		}, 24, &TestUnion3{})
	})
	t.Run("Handles", func(t *testing.T) {
		vmo, err := zx.NewVMO(10, 0)
		if err != nil {
			t.Fatalf("failed to create vmo: %v", err)
		}
		testIdentity(t, &TestHandle1{
			A: zx.Handle(22),
			B: zx.HandleInvalid,
			C: vmo,
			D: zx.VMO(zx.HandleInvalid),
		}, 16, &TestHandle1{})
		testIdentity(t, &TestHandle2{
			A: []zx.Handle{zx.Handle(vmo)},
			B: []zx.VMO{zx.VMO(zx.HandleInvalid)},
		}, 48, &TestHandle2{})
		vmo.Close()
	})
	t.Run("Interfaces", func(t *testing.T) {
		h0, h1, err := zx.NewChannel(0)
		defer h0.Close()
		defer h1.Close()
		if err != nil {
			t.Fatalf("failed to create vmo: %v", err)
		}
		testIdentity(t, &TestInterface1{
			A: Test1Interface(Proxy{Channel: h0}),
			B: Test1Interface(Proxy{Channel: zx.Channel(zx.HandleInvalid)}),
			C: Test1InterfaceRequest(InterfaceRequest{Channel: h1}),
			D: Test1InterfaceRequest(InterfaceRequest{
				Channel: zx.Channel(zx.HandleInvalid),
			}),
		}, 16, &TestInterface1{})
	})
}
