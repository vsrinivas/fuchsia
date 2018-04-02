// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	_zx "syscall/zx"
)

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
	return 32
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
	return 16
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
	A [2]string  `fidl:"4"`
	B [2]*string `fidl:"4"`
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
	C []int32  `fidl:"2"`
	D *[]int64 `fidl:"2"`
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
	B [][]int8    `fidl:",2"`
	C []*[]string `fidl:"5,2,2"`
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
	B *TestSimple
}

// Implements Payload.
func (_ *TestStruct1) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestStruct1) InlineSize() int {
	return 16
}

type TestStruct2 struct {
	A TestArray1
	B TestFloat1
	C TestVector1
	D *TestString1
}

// Implements Payload.
func (_ *TestStruct2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestStruct2) InlineSize() int {
	return 88
}

type TestUnion1 struct {
	A Union1
	B *Union1
}

// Implements Payload.
func (_ *TestUnion1) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestUnion1) InlineSize() int {
	return 24
}

type TestUnion2 struct {
	A []Union1
	B []*Union1
}

// Implements Payload.
func (_ *TestUnion2) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *TestUnion2) InlineSize() int {
	return 32
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
	A []_zx.Handle `fidl:"1"`
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

type TestInterface1 struct {
	A Test1Interface
	B Test1Interface `fidl:"*"`
	C Test1InterfaceRequest
	D Test1InterfaceRequest `fidl:"*"`
}

// Implements Payload.
func (_ *TestInterface1) InlineAlignment() int {
	return 4
}

// Implements Payload.
func (_ *TestInterface1) InlineSize() int {
	return 16
}

type Union1Tag uint32

const (
	Union1A Union1Tag = iota
	Union1B Union1Tag = iota
	Union1C Union1Tag = iota
	Union1D Union1Tag = iota
)

// Union1 is a FIDL union.
type Union1 struct {
	Union1Tag `fidl:"tag"`
	A         [3]int8
	B         TestSimple
	C         *TestSimple
	D         float32
}

// Implements Payload.
func (_ *Union1) InlineAlignment() int {
	return 8
}

// Implements Payload.
func (_ *Union1) InlineSize() int {
	return 16
}

func (u *Union1) Which() Union1Tag {
	return u.Union1Tag
}

func (u *Union1) SetA(a [3]int8) {
	u.Union1Tag = Union1A
	u.A = a
}

func (u *Union1) SetB(b TestSimple) {
	u.Union1Tag = Union1B
	u.B = b
}

func (u *Union1) SetC(c *TestSimple) {
	u.Union1Tag = Union1C
	u.C = c
}

func (u *Union1) SetD(d float32) {
	u.Union1Tag = Union1D
	u.D = d
}

// Request for Echo.
type Test1EchoRequest struct {
	In *string
}

// Implements Payload.
func (_ *Test1EchoRequest) InlineAlignment() int {
	return 0
}

// Implements Payload.
func (_ *Test1EchoRequest) InlineSize() int {
	return 16
}

// Response for Echo.
type Test1EchoResponse struct {
	Out *string
}

// Implements Payload.
func (_ *Test1EchoResponse) InlineAlignment() int {
	return 0
}

// Implements Payload.
func (_ *Test1EchoResponse) InlineSize() int {
	return 16
}

type Test1Interface Proxy

func (p *Test1Interface) Echo(in *string) (*string, error) {
	req_ := Test1EchoRequest{
		In: in,
	}
	resp_ := Test1EchoResponse{}
	err := ((*Proxy)(p)).Call(10, &req_, &resp_)
	return resp_.Out, err
}

type Test1 interface {
	Echo(in *string) (out *string, err_ error)
}

type Test1InterfaceRequest InterfaceRequest

func NewTest1InterfaceRequest() (Test1InterfaceRequest, *Test1Interface, error) {
	req, cli, err := NewInterfaceRequest()
	return Test1InterfaceRequest(req), (*Test1Interface)(cli), err
}

type Test1Stub struct {
	Impl Test1
}

func (s *Test1Stub) Dispatch(ord uint32, b_ []byte, h_ []_zx.Handle) (Payload, error) {
	switch ord {
	case 10:
		in_ := Test1EchoRequest{}
		if err_ := Unmarshal(b_, h_, &in_); err_ != nil {
			return nil, err_
		}
		out_ := Test1EchoResponse{}
		out, err_ := s.Impl.Echo(in_.In)
		out_.Out = out
		return &out_, err_
	}
	return nil, ErrUnknownOrdinal
}
