// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestSimpleUtf8Chars(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
Copy-pasting nicely formatted documents can lead to «issues…»

For instance,

- fancy open quotes like «“so»
- fancy close quotes like «so”»
- or fancy single quotes like «d’Auzac» de Lamartinie
- or fancy open single quotes like «‘»
- or all together «“d’Et-bugge”»`,
		},
	}.runOverTokens(t, newSimpleUtf8Chars)
}
