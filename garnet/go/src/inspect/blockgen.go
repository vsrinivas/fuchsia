// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build ignore

// blockgen generates function definitions for various inspect VMO block types.
//
// All block types share some common header information, but individual block types interpret
// payload data differently. This program generates the block.go file for the inspect package
// such that each block type is properly generated.
package main

import (
	"bytes"
	"fmt"
	"go/format"
	"os"
	"text/template"
)

// BitField describes information about bit-packed fields for each block. This
// includes the name of the field, the 0-indexed bit position for the start of the
// value, and the 0-indexed bit position of the end of the value.
type BitField struct {
	// Name represents the name of this bitfield.
	Name string

	// BitStart represents the least significant bit's starting position for this field within
	// the encompassing value.
	BitStart uint

	// BitEnd represents the position of the most significant bit for this field within the
	// encompassing value.
	BitEnd uint

	// Type represents the type of the field.
	Type string
}

// Mask generates a bitmask for the BitField.
func (b BitField) Mask() uint64 {
	bits := b.BitEnd - b.BitStart + 1
	mask := (1 << bits) - 1
	return uint64(mask << b.BitStart)
}

// BlockInfo describes the layout of a kind of block.
type BlockInfo struct {
	// Name represents the name of the type of block being described.
	Name string

	// HeaderFields describes the bitfields packed into the header quadword.
	HeaderFields []BitField

	// PayloadFields describes the bitfields packed into the payload quadword.
	PayloadFields []BitField

	// PayloadOffset describes the position of any additional arbitrary / variable length payload for this block.
	PayloadOffset uint64
}

var CommonHeaderFields = []BitField{
	{"Order", 0, 3, "BlockOrder"},
	{"Type", 4, 7, "BlockType"},
}

func (bi BlockInfo) AllHeaders() []BitField {
	return append(CommonHeaderFields, bi.HeaderFields...)
}

var BlockTypes = []BlockInfo{
	{Name: "Block"},
	{
		Name:         "FreeBlock",
		HeaderFields: []BitField{{"NextFree", 8, 35, "BlockIndex"}},
	},
	{Name: "ReservedBlock"},
	{
		Name: "HeaderBlock",
		HeaderFields: []BitField{
			{"Version", 8, 31, "uint"},
			{"Magic", 32, 63, "uint"},
		},
	},
	{
		Name: "NodeValueBlock",
		HeaderFields: []BitField{
			{"ParentIndex", 8, 35, "BlockIndex"},
			{"NameIndex", 36, 63, "BlockIndex"},
		},
		PayloadOffset: 8,
	},
	{
		Name: "IntValueBlock",
		HeaderFields: []BitField{
			{"ParentIndex", 8, 35, "BlockIndex"},
			{"NameIndex", 36, 63, "BlockIndex"},
		},
		PayloadFields: []BitField{{"Value", 0, 63, "int64"}},
	},
	{
		Name: "UintValueBlock",
		HeaderFields: []BitField{
			{"ParentIndex", 8, 35, "uint"},
			{"NameIndex", 36, 63, "uint"},
		},
		PayloadFields: []BitField{{"Value", 0, 63, "uint64"}},
	},
	{
		Name: "DoubleValueBlock",
		HeaderFields: []BitField{
			{"ParentIndex", 8, 35, "BlockIndex"},
			{"NameIndex", 36, 63, "BlockIndex"},
		},
		PayloadFields: []BitField{{"Value", 0, 63, "float64"}},
	},
	{
		Name: "PropertyBlock",
		HeaderFields: []BitField{
			{"ParentIndex", 8, 35, "BlockIndex"},
			{"NameIndex", 36, 63, "BlockIndex"},
		},
		PayloadFields: []BitField{
			{"Length", 0, 31, "uint"},
			{"ExtentIndex", 32, 59, "uint"},
			{"Flags", 60, 63, "uint"},
		},
	},
	{
		Name:         "ExtentBlock",
		HeaderFields: []BitField{{"NextExtent", 8, 35, "BlockIndex"}},
	},
	{
		Name:          "NameBlock",
		HeaderFields:  []BitField{{"Length", 8, 19, "uint"}},
		PayloadOffset: 8,
	},
	{
		Name:          "TombstoneBlock",
		PayloadFields: []BitField{{"RefCount", 0, 63, "uint64"}},
	},
	{
		Name: "ArrayBlock",
		HeaderFields: []BitField{
			{"ParentIndex", 8, 35, "BlockIndex"},
			{"NameIndex", 36, 63, "BlockIndex"},
		},
		PayloadFields: []BitField{
			{"EntryType", 0, 3, "uint"},
			{"Flags", 4, 7, "uint"},
			{"Count", 8, 15, "uint"},
		},
		PayloadOffset: 16,
	},
	{
		Name: "LinkBlock",
		PayloadFields: []BitField{
			{"ContextIndex", 0, 19, "uint"},
			{"Flags", 60, 63, "uint"},
		},
	},
}

var BlockOrders = []int{16, 32, 64, 128, 256, 512, 1024, 2048}

func main() {
	tmpl, err := template.ParseFiles("block.tmpl")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to parse template: %v\n", err)
		os.Exit(1)
	}

	b := bytes.NewBuffer([]byte{})
	tmpl.Execute(b, map[string]interface{}{
		"BlockTypes":         BlockTypes,
		"BlockOrders":        BlockOrders,
		"CommonHeaderFields": CommonHeaderFields,
	})

	src, err := format.Source(b.Bytes())
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to format source: %v\n", err)
		fmt.Fprintf(os.Stderr, "%s\n", b.String())
		os.Exit(1)
	}

	f, err := os.OpenFile("block.go", os.O_RDWR|os.O_CREATE, 0644)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Couldn't open block.go for writing: %v\n", err)
		os.Exit(1)
	}

	if err := f.Truncate(0); err != nil {
		fmt.Fprintf(os.Stderr, "Couldn't truncate block.go: %v\n", err)
		os.Exit(1)
	}

	fmt.Fprint(f, string(src))
}
