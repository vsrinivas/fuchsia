// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.constants (//zircon/tools/zither/testdata/constants/constants.test.fidl)
// by zither, a Fuchsia platform tool.

package constants

const Uint8Zero uint8 = 0

const Uint8MaxDec uint8 = 255

const Uint8MaxHex uint8 = 0xff

const Int8Zero int8 = 0

const Int8MinDec int8 = -128

const Int8MinHex int8 = -0x80

const Int8MaxDec int8 = 127

const Int8MaxHex int8 = 0x7f

const Uint16Zero uint16 = 0

const Uint16MaxDec uint16 = 65535

const Uint16MaxHex uint16 = 0xffff

const Int16Zero int16 = 0

const Int16MinDec int16 = -32768

const Int16MinHex int16 = -0x8000

const Int16MaxDec int16 = 32767

const Int16MaxHex int16 = 0x7fff

const Uint32Zero uint32 = 0

const Uint32MaxDec uint32 = 4294967295

const Uint32MaxHex uint32 = 0xffffffff

const Int32Zero int32 = 0

const Int32MinDec int32 = -2147483648

const Int32MinHex int32 = -0x80000000

const Int32MaxDec int32 = 2147483647

const Int32MaxHex int32 = 0x7fffffff

const Uint64Zero uint64 = 0

const Uint64MaxDec uint64 = 18446744073709551615

const Uint64MaxHex uint64 = 0xffffffffffffffff

const Int64Zero int64 = 0

const Int64MinDec int64 = -9223372036854775808

const Int64MinHex int64 = -0x8000000000000000

const Int64MaxDec int64 = 9223372036854775807

const Int64MaxHex int64 = 0x7fffffffffffffff

const False bool = false

const True bool = true

const EmptyString string = ""

const ByteZero uint8 = 0

const BinaryValue uint8 = 0b10101111

const LowercaseHexValue uint64 = 0x1234abcd5678ffff

const UppercaseHexValue uint64 = 0x1234ABCD5678FFFF

const LeadingZeroesHexValue uint32 = 0x00000011

const LeadingZeroesDecValue uint32 = 0000000017

const LeadingZeroesBinaryValue uint32 = 0b0000000000010001

const BitwiseOrValue uint8 = 15 // 0b1000 | 0b0100 | 0b0010 | 0b0001

const NonemptyString string = "this is a constant"

const DefinitionFromAnotherConstant string = NonemptyString

const BitwiseOrOfOtherConstants uint8 = 175 // BINARY_VALUE | BITWISE_OR_VALUE | 0b1 | UINT8_ZERO

// Constant with a one-line comment.
const ConstantOneLineComment bool = true

// Constant
//
//	with
//	    a
//	      many-line
//	        comment.
const ConstantManyLineComment string = ""
