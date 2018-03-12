// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import _zx "syscall/zx"

type TestSimple struct {
	X int64
}

// Implements Payload.
func (_ *TestSimple) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestSimple) InlineSize() int {
	return 8
}

type TestSimpleBool struct {
	X bool
}

// Implements Payload.
func (_ *TestSimpleBool) InlineAlignment() int {
	return 1
}

// Implements Payload.
func (_ *TestSimpleBool) InlineSize() int {
	return 1
}

type TestAlignment1 struct {
	X int8
	Y int8
	Z uint32
}

// Implements Payload.
func (_ *TestAlignment1) InlineAlignment() int {
	return 4
}

// Implements Payload.
func (_ *TestAlignment1) InlineSize() int {
	return 8
}

type TestAlignment2 struct {
	A uint32
	B uint32
	C int8
	D int8
	E int8
	F uint8
	G uint32
	H uint16
	I uint16
}

// Implements Payload.
func (_ *TestAlignment2) InlineAlignment() int {
	return 4
}

// Implements Payload.
func (_ *TestAlignment2) InlineSize() int {
	return 20
}

type TestFloat1 struct {
	A float32
}

// Implements Payload.
func (_ *TestFloat1) InlineAlignment() int {
	return 4
}

// Implements Payload.
func (_ *TestFloat1) InlineSize() int {
	return 4
}

type TestFloat2 struct {
	A float64
}

// Implements Payload.
func (_ *TestFloat2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestFloat2) InlineSize() int {
	return 8
}

type TestFloat3 struct {
	A float32
	B float64
	C uint64
	D float32
}

// Implements Payload.
func (_ *TestFloat3) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestFloat3) InlineSize() int {
	return 28
}

type TestArray1 struct {
	A [5]int8
}

// Implements Payload.
func (_ *TestArray1) InlineAlignment() int {
	return 1
}

// Implements Payload.
func (_ *TestArray1) InlineSize() int {
	return 5
}

type TestArray2 struct {
	A float64
	B [1]float32
}

// Implements Payload.
func (_ *TestArray2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestArray2) InlineSize() int {
	return 12
}

type TestArray3 struct {
	A int32
	B [3]uint16
	C uint64
}

// Implements Payload.
func (_ *TestArray3) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestArray3) InlineSize() int {
	return 24
}

type TestArray4 struct {
	A [9]bool
}

// Implements Payload.
func (_ *TestArray4) InlineAlignment() int {
	return 1
}

// Implements Payload.
func (_ *TestArray4) InlineSize() int {
	return 9
}

type TestString1 struct {
	A string
	B *string
}

// Implements Payload.
func (_ *TestString1) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestString1) InlineSize() int {
	return 32
}

type TestString2 struct {
	A [2]string
}

// Implements Payload.
func (_ *TestString2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestString2) InlineSize() int {
	return 32
}

type TestString3 struct {
	A [2]string  `fidl:",4"`
	B [2]*string `fidl:",4"`
}

// Implements Payload.
func (_ *TestString3) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestString3) InlineSize() int {
	return 64
}

type TestVector1 struct {
	A []int8
	B *[]int16
	C []int32  `fidl:",2"`
	D *[]int64 `fidl:",2"`
}

// Implements Payload.
func (_ *TestVector1) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestVector1) InlineSize() int {
	return 64
}

type TestVector2 struct {
	A [2][]int8
	B [][]int8    `fidl:",,2"`
	C []*[]string `fidl:",5,2,2"`
}

// Implements Payload.
func (_ *TestVector2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestVector2) InlineSize() int {
	return 64
}

type TestStruct1 struct {
	A TestSimple
}

// Implements Payload.
func (_ *TestStruct1) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestStruct1) InlineSize() int {
	return 8
}

type TestStruct2 struct {
	A TestArray1
	B TestFloat1
	C TestVector1
}

// Implements Payload.
func (_ *TestStruct2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestStruct2) InlineSize() int {
	return 80
}

type TestHandle1 struct {
	A _zx.Handle
	B _zx.Handle `fidl:"*"`
	C _zx.VMO
	D _zx.VMO `fidl:"*"`
}

// Implements Payload.
func (_ *TestHandle1) InlineAlignment() int {
	return 4
}

// Implements Payload.
func (_ *TestHandle1) InlineSize() int {
	return 16
}

type TestHandle2 struct {
	A []_zx.Handle `fidl:",1"`
	B []_zx.VMO    `fidl:"*,1"`
}

// Implements Payload.
func (_ *TestHandle2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestHandle2) InlineSize() int {
	return 32
}
