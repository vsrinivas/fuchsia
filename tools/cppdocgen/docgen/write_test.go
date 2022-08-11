// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"reflect"
	"testing"
)

func TestExtractCommentHeading1(t *testing.T) {
	h, r := extractCommentHeading1([]clangdoc.CommentInfo{})
	if len(h) != 0 || len(r) != 0 {
		t.Errorf("Expected no heading for empty comment.")
	}

	contentNoComment := []clangdoc.CommentInfo{
		{Kind: "TextComment", Text: " Not a heading 1"},
		{Kind: "TextComment", Text: " NextLine"},
	}
	h, r = extractCommentHeading1(contentNoComment)
	if len(h) != 0 || !reflect.DeepEqual(contentNoComment, r) {
		t.Errorf("Expected no heading")
	}

	contentComment := []clangdoc.CommentInfo{
		{Kind: "TextComment", Text: " # A heading 1"},
		{Kind: "TextComment", Text: " Next line"},
	}
	h, r = extractCommentHeading1(contentComment)
	if h != "# A heading 1" || r[0].Text != " Next line" {
		t.Errorf("Expected heading")
	}

	nested := []clangdoc.CommentInfo{
		{
			Kind: "FullComment",
			Children: []clangdoc.CommentInfo{
				{
					Kind: "ParagraphComment",
					Children: []clangdoc.CommentInfo{
						{
							Kind: "TextComment",
							Text: " # This has a heading",
						},
						{
							Kind: "TextComment",
							Text: " Next line",
						},
					},
				},
				{
					Kind: "ParagraphComment",
					Children: []clangdoc.CommentInfo{
						{
							Kind: "TextComment",
							Text: " Third line",
						},
					},
				},
			},
		},
	}

	// Generate the expected result with the same structure but the first line removed.
	expected := make([]clangdoc.CommentInfo, len(nested))
	copy(expected, nested)
	firstParaChildren := expected[0].Children[0].Children
	firstParaChildren = firstParaChildren[1:]

	h, r = extractCommentHeading1(nested)
	if h != "# This has a heading" || !reflect.DeepEqual(expected, nested) {
		t.Errorf("Expected:\n  %v\nGot\n  %v", expected, r)
	}
}
