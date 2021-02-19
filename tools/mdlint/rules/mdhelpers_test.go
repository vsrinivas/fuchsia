// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestNormalizeLinkLabel(t *testing.T) {
	normalized := "this is the same"
	allTheSame := []string{
		normalized,
		"   THIS is the\tsame  ",
		"tHiS\n\n is    the Same",
		"\nTHIS  IS  THE  SAME\n",
	}
	for _, example := range allTheSame {
		actual := normalizeLinkLabel(example)
		if actual != normalized {
			t.Errorf("%s: '%s' but normalized is '%s'", example, actual, normalized)
		}
	}
}
