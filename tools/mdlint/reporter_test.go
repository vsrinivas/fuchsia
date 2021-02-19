// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestMessagesToFindingsJSON(t *testing.T) {
	var (
		doc          = doc{filename: "path/to/the/file.md"}
		rootReporter RootReporter
		ruleReporter = rootReporter.ForRule("some-rule")
	)

	rootReporter.Warnf(token{
		doc:     &doc,
		kind:    tText,
		content: "1234",
		ln:      10, // i.e. 10th line
		col:     1,  // i.e. first character
	}, "%s, %s!", "Hello", "World")
	rootReporter.Warnf(token{
		doc:     &doc,
		kind:    tText,
		content: "```this one\nspans\nmultiple lines\n123456```",
		ln:      2, // i.e. 2nd line
		col:     5, // i.e. fifth character
	}, "%d%d!", 4, 2)
	ruleReporter.Warnf(token{
		doc:     &doc,
		kind:    tText,
		content: "1",
		ln:      30, // i.e. 30th line
		col:     17, // i.e. seventeeth character
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

	actual := rootReporter.messagesToFindingsJSON()
	if diff := cmp.Diff(expected, actual); diff != "" {
		t.Errorf("expected != actual (-want +got)\n%s", diff)
	}
}
