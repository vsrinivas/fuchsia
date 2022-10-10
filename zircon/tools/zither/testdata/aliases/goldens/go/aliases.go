// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT.
// Generated from FIDL library `zither.aliases` by zither, a Fuchsia platform tool.

package aliases

type BoolAlias = bool

type Int8Alias = int8

type Int16Alias = int16

type Int32Alias = int32

type Int64Alias = int64

type Uint8Alias = uint8

type Uint16Alias = uint16

type Uint32Alias = uint32

type Uint64Alias = uint64

// TODO(fxbug.dev/105758): The IR currently does not propagate enough
// information for bindings to express this type as an alias.
const ConstFromAlias uint8 = 0xff

type Enum int16

const (
	EnumMember Enum = 0
)

type EnumAlias = Enum

type Bits uint16

const (
	BitsOne Bits = 1
)

type BitsAlias = Bits

type Struct struct {
	X uint64
	Y uint64
}

type StructAlias = Struct

type ArrayAlias = [4]uint32

type NestedArrayAlias = [4][8]Struct

// Alias with a one-line comment.
type AliasWithOneLineComment = bool

// Alias
//
//	with
//	    a
//	      many-line
//	        comment.
type AliasWithManyLineComment = uint8
