// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"testing"
)

func TestFixupLinks(t *testing.T) {
	index := makeEmptyIndex()

	// Functions are indexed by both name and USR.
	indexedFunction := clangdoc.FunctionInfo{
		USR:      "SOME_FUNCTION_USR",
		Name:     "indexed_symbol",
		Location: []clangdoc.Location{{LineNumber: 16, Filename: "filename.h"}},
	}
	index.FunctionUsrs[indexedFunction.USR] = &indexedFunction
	index.FunctionNames[indexedFunction.Name] = &indexedFunction

	index.Defines["INDEXED_DEFINE"] = &Define{
		Name:     "INDEXED_DEFINE",
		Location: clangdoc.Location{LineNumber: 16, Filename: "filename.h"},
	}
	index.Enums["IndexedEnum"] = &clangdoc.EnumInfo{
		Name:        "IndexedEnum",
		DefLocation: clangdoc.Location{LineNumber: 16, Filename: "filename.h"},
	}

	// Records are indexed by both name USR.
	indexedRecord := clangdoc.RecordInfo{
		USR:         "SOME_USR",
		Name:        "indexed_struct",
		DefLocation: clangdoc.Location{LineNumber: 16, Filename: "filename.h"},
	}
	index.RecordNames[indexedRecord.Name] = &indexedRecord
	index.RecordUsrs[indexedRecord.USR] = &indexedRecord

	// Comment with no links.
	output := fixupLinks(&index, "comment blah")
	if output != "comment blah" {
		t.Errorf("Unlinked comment is wrong: %v\n", output)
	}

	// Comment with link and no matching name should be passed unchanged.
	output = fixupLinks(&index, "comment [not_found_symbol] something")
	if output != "comment [not_found_symbol] something" {
		t.Errorf("Not found link comment is wrong: %v\n", output)
	}

	// Comment with markdown link should be passed unchanged.
	output = fixupLinks(&index, "comment [indexed_symbol](here.md)")
	if output != "comment [indexed_symbol](here.md)" {
		t.Errorf("Markdown comment is wrong: %v\n", output)
	}

	// Found symbol should be converted to the proper link.
	output = fixupLinks(&index, "comment [indexed_symbol] more")
	if output != "comment [`indexed_symbol`](filename.h.md#indexed_symbol) more" {
		t.Errorf("Linked comment is wrong: %v\n", output)
	}

	// Parens on the symbol should be included in the output but not count in the symbol lookup.
	output = fixupLinks(&index, "comment [INDEXED_DEFINE()] more")
	if output != "comment [`INDEXED_DEFINE()`](filename.h.md#INDEXED_DEFINE) more" {
		t.Errorf("Linked comment is wrong: %v\n", output)
	}
	output = fixupLinks(&index, "comment [indexed_symbol(param)] more")
	if output != "comment [`indexed_symbol(param)`](filename.h.md#indexed_symbol) more" {
		t.Errorf("Linked comment is wrong: %v\n", output)
	}

	// Two links on the same line, this also checks records and enums.
	output = fixupLinks(&index, "a [indexed_struct] b [IndexedEnum]")
	if output != "a [`indexed_struct`](filename.h.md#indexed_struct) b [`IndexedEnum`](filename.h.md#IndexedEnum)" {
		t.Errorf("Linked comment is wrong: %v\n", output)
	}
}
