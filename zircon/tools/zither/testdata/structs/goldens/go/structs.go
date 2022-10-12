// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.structs (//zircon/tools/zither/testdata/structs/structs.test.fidl)
// by zither, a Fuchsia platform tool.

package structs

type Empty struct {
}

type Singleton struct {
	Value uint8
}

type Doubtleton struct {
	First  Singleton
	Second Singleton
}

type PrimitiveMembers struct {
	I64 int64
	U64 uint64
	I32 int32
	U32 uint32
	I16 int16
	U16 uint16
	I8  int8
	U8  uint8
	B   bool
}

type ArrayMembers struct {
	U8s           [10]uint8
	Singletons    [6]Singleton
	NestedArrays1 [20][10]uint8
	NestedArrays2 [3][2][1]int8
}

// Struct with a one-line comment.
type StructWithOneLineComment struct {

	// Struct member with one-line comment.
	MemberWithOneLineComment uint32

	// Struct member
	//     with a
	//         many-line
	//           comment.
	MemberWithManyLineComment bool
}

// Struct
//
//	with a
//	    many-line
//	      comment.
type StructWithManyLineComment struct {
	Member uint16
}
