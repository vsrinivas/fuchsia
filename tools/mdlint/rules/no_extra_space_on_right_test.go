// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestNoExtraSpaceOnRight(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `example

markdown here«  »

and then this.`,
		},
	}.runOverTokens(t, newNoExtraSpaceOnRight)
}
