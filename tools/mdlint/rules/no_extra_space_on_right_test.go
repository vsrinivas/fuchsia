// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestNoExtraSpaceOnRight(t *testing.T) {
	ruleTestCase{
		input: `example

markdown here«  »

and then this.`,
	}.runOverTokens(t, newNoExtraSpaceOnRight)
}
