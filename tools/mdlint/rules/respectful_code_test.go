// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestRespectifulCode(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"bad_usage.md": `
			«master» controller.
			`,
			"sub_string.md": `
			masterful.
			`,
			"heading.md": `
			«#slave»
			`,
			"end_of_setence.md": `
			I'm «insane.»
			`,
			"broken_word.md": `
			I'm a «man-in-the-middle.»
			`,
		},
	}.runOverTokens(t, newRespectfulCode)
}
