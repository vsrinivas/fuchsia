// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestNoNewlineBeforeCodeBlock(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"no_new_line.md":     "example\nmarkdown here «```java\nblah\n```»\n",
			"one_new_line.md":    "example\nmarkdown here\n«```java\nblah\n```»\n",
			"two_new_lines.md":   "example\nmarkdown here\n\n```java\nblah\n```\n",
			"three_new_lines.md": "example\nmarkdown here\n\n\n```java\nblah\n\ntest\n```\n",
			// Another lint rule should warn about the empty space on the
			// right, but this rule should work.
			"two_new_lines_with_spaces.md": "example\nmarkdown here \n \n```java\nblah\n```\n",
		},
	}.runOverTokens(t, newNewlineBeforeCodeSpan)
}
