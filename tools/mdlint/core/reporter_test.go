// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestMessagesToFindingsJSON(t *testing.T) {
	var (
		doc          = Doc{Filename: "path/to/the/file.md"}
		rootReporter RootReporter
		ruleReporter = rootReporter.ForRule("some-rule")
	)

	rootReporter.Warnf(Token{
		Doc:     &doc,
		Kind:    Text,
		Content: "1234",
		Ln:      10, // i.e. 10th line
		Col:     1,  // i.e. first character
	}, "%s, %s!", "Hello", "World")
	rootReporter.Warnf(Token{
		Doc:     &doc,
		Kind:    Text,
		Content: "```this one\nspans\nmultiple lines\n123456```",
		Ln:      2, // i.e. 2nd line
		Col:     5, // i.e. fifth character
	}, "%d%d!", 4, 2)
	ruleReporter.Warnf(Token{
		Doc:     &doc,
		Kind:    Text,
		Content: "1",
		Ln:      30, // i.e. 30th line
		Col:     17, // i.e. seventeeth character
	}, "no format")

	expected := []findingJSON{
		{
			Category:  "mdlint/general",
			Message:   "42!",
			Path:      "path/to/the/file.md",
			StartLine: 2,
			StartChar: 4,
			EndLine:   5,
			EndChar:   9,
		},
		{
			Category:  "mdlint/general",
			Message:   "Hello, World!",
			Path:      "path/to/the/file.md",
			StartLine: 10,
			StartChar: 0,
			EndLine:   10,
			EndChar:   4,
		},
		{
			Category:  "mdlint/some-rule",
			Message:   "no format",
			Path:      "path/to/the/file.md",
			StartLine: 30,
			StartChar: 16,
			EndLine:   30,
			EndChar:   17,
		},
	}

	actual := rootReporter.messagesToFindingsJSON(allFilenames)
	if diff := cmp.Diff(expected, actual); diff != "" {
		t.Errorf("expected != actual (-want +got)\n%s", diff)
	}
}
