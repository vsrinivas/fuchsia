// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestNoExtraSpaceAtStartOfDoc(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
«New» line at start of file.
`,
			"example2.md": `  ` + `

«New» and space line at start of file.
`,
			"example3.md": `


  «A» really poorly formatted Markdown file!`,
		},
	}.runOverTokens(t, newNoExtraSpaceAtStartOfDoc)
}
