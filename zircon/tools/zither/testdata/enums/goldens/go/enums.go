// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.enums (//zircon/tools/zither/testdata/enums/enums.test.fidl)
// by zither, a Fuchsia platform tool.

package enums

type Color uint8

const (
	ColorRed    Color = 0
	ColorOrange Color = 1
	ColorYellow Color = 2
	ColorGreen  Color = 3
	ColorBlue   Color = 4
	ColorIndigo Color = 5
	ColorViolet Color = 6
)

type Uint8Limits uint8

const (
	Uint8LimitsMin Uint8Limits = 0
	Uint8LimitsMax Uint8Limits = 0b11111111
)

type Uint16Limits uint16

const (
	Uint16LimitsMin Uint16Limits = 0
	Uint16LimitsMax Uint16Limits = 0xffff
)

type Uint32Limits uint32

const (
	Uint32LimitsMin Uint32Limits = 0
	Uint32LimitsMax Uint32Limits = 0xffffffff
)

type Uint64Limits uint64

const (
	Uint64LimitsMin Uint64Limits = 0
	Uint64LimitsMax Uint64Limits = 0xffffffffffffffff
)

type Int8Limits int8

const (
	Int8LimitsMin Int8Limits = -0x80
	Int8LimitsMax Int8Limits = 0x7f
)

type Int16Limits int16

const (
	Int16LimitsMin Int16Limits = -0x8000
	Int16LimitsMax Int16Limits = 0x7fff
)

type Int32Limits int32

const (
	Int32LimitsMin Int32Limits = -0x80000000
	Int32LimitsMax Int32Limits = 0x7fffffff
)

type Int64Limits int64

const (
	Int64LimitsMin Int64Limits = -0x8000000000000000
	Int64LimitsMax Int64Limits = 0x7fffffffffffffff
)

// Enum with a one-line comment.
type EnumWithOneLineComment uint8

const (

	// Enum member with one-line comment.
	EnumWithOneLineCommentMemberWithOneLineComment EnumWithOneLineComment = 0

	// Enum member
	//     with a
	//         many-line
	//           comment.
	EnumWithOneLineCommentMemberWithManyLineComment EnumWithOneLineComment = 1
)

// Enum
//
//	with a
//	    many-line
//	      comment.
type EnumWithManyLineComment uint16

const (
	EnumWithManyLineCommentMember EnumWithManyLineComment = 0
)

const Red Color = ColorRed

const Uint8Min Uint8Limits = Uint8LimitsMin

const Uint8Max Uint8Limits = Uint8LimitsMax

const Uint16Min Uint16Limits = Uint16LimitsMin

const Uint16Max Uint16Limits = Uint16LimitsMax

const Uint32Min Uint32Limits = Uint32LimitsMin

const Uint32Max Uint32Limits = Uint32LimitsMax

const Uint64Min Uint64Limits = Uint64LimitsMin

const Uint64Max Uint64Limits = Uint64LimitsMax

const Int8Min Int8Limits = Int8LimitsMin

const Int8Max Int8Limits = Int8LimitsMax

const Int16Min Int16Limits = Int16LimitsMin

const Int16Max Int16Limits = Int16LimitsMax

const Int32Min Int32Limits = Int32LimitsMin

const Int32Max Int32Limits = Int32LimitsMax

const Int64Min Int64Limits = Int64LimitsMin

const Int64Max Int64Limits = Int64LimitsMax
