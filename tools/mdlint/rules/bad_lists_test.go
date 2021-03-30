// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestBadLists_overall(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
When making lists:
«*» it is easy to forget
* that a blank line is required.

* This is a new list, and we saw a blank line, all is good.

But then, here:
«*» A blank is required again.

## Even after important headers
«*» A blank is required to start a list right off the bat.`,
		},
	}.runOverTokens(t, newBadLists)
}

func TestBadLists_startWithList(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `- A list, immediately.`,
		},
	}.runOverTokens(t, newBadLists)
}

func TestBadLists_listWithList(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `1. First element.
2. Second element.
   - Sub-element of second element.
   - Second sub-element of second element.`,
		},
	}.runOverTokens(t, newBadLists)
}

func TestBadLists_falsePositive(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
Some sentences end in numbers. For example, the number of countries on Earth is
«195.» This looks like a list, but it is not.

To work around this, you can escape the period with a backslash:
195\. This one didn't cause a warning.`,
		},
	}.runOverTokens(t, newBadLists)
}
