// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.bits (//zircon/tools/zither/testdata/bits/bits.test.fidl)
// by zither, a Fuchsia platform tool.

package bits

type Uint8Bits uint8

const (
	Uint8BitsOne                   Uint8Bits = 1 << 0
	Uint8BitsTwo                   Uint8Bits = 1 << 1
	Uint8BitsFour                  Uint8Bits = 1 << 2
	Uint8BitsEight                 Uint8Bits = 1 << 3
	Uint8BitsSixteen               Uint8Bits = 1 << 4
	Uint8BitsThirtyTwo             Uint8Bits = 1 << 5
	Uint8BitsSixtyFour             Uint8Bits = 1 << 6
	Uint8BitsOneHundredTwentyEight Uint8Bits = 1 << 7
)

type Uint16Bits uint16

const (
	Uint16BitsZeroth      Uint16Bits = 1 << 0
	Uint16BitsFirst       Uint16Bits = 1 << 1
	Uint16BitsSecond      Uint16Bits = 1 << 2
	Uint16BitsThird       Uint16Bits = 1 << 3
	Uint16BitsFourth      Uint16Bits = 1 << 4
	Uint16BitsFifth       Uint16Bits = 1 << 5
	Uint16BitsSixth       Uint16Bits = 1 << 6
	Uint16BitsSeventh     Uint16Bits = 1 << 7
	Uint16BitsEight       Uint16Bits = 1 << 8
	Uint16BitsNinth       Uint16Bits = 1 << 9
	Uint16BitsTenth       Uint16Bits = 1 << 10
	Uint16BitsEleventh    Uint16Bits = 1 << 11
	Uint16BitsTwelfth     Uint16Bits = 1 << 12
	Uint16BitsThirteenth  Uint16Bits = 1 << 13
	Uint16BitsFourteenth  Uint16Bits = 1 << 14
	Uint16BitsFifthteenth Uint16Bits = 1 << 15
)

type Uint32Bits uint32

const (
	Uint32BitsPow0  Uint32Bits = 1 << 0
	Uint32BitsPow31 Uint32Bits = 1 << 31
)

type Uint64Bits uint64

const (
	Uint64BitsPow0  Uint64Bits = 1 << 0
	Uint64BitsPow63 Uint64Bits = 1 << 63
)

// Bits with a one-line comment.
type BitsWithOneLineComment uint8

const (

	// Bits member with one-line comment.
	BitsWithOneLineCommentMemberWithOneLineComment BitsWithOneLineComment = 1 << 0

	// Bits member
	//     with a
	//         many-line
	//           comment.
	BitsWithOneLineCommentMemberWithManyLineComment BitsWithOneLineComment = 1 << 6
)

// Bits
//
//	with a
//	    many-line
//	      comment.
type BitsWithManyLineComment uint16

const (
	BitsWithManyLineCommentMember BitsWithManyLineComment = 1 << 0
)

const SeventyTwo Uint8Bits = 0b1001000 // Uint8Bits.SIXTY_FOUR | Uint8Bits.EIGHT

const SomeBits Uint16Bits = 0b1001000000010 // Uint16Bits.FIRST | Uint16Bits.NINTH | Uint16Bits.TWELFTH

const U32Pow0 Uint32Bits = Uint32BitsPow0

const U64Pow63 Uint64Bits = Uint64BitsPow63
