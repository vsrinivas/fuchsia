// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ignores

import "testing"

func TestShouldIgnore(t *testing.T) {
	shouldIgnore := []string{
		".git",
		".jiri",
		".hg",
		"a/.git",
		".git/a",
		".git/.git",
		".git/../.git",
	}

	shouldNotIgnore := []string{
		"a",
		"git",
		"jiri",
		"a/a",
		"a/../a",
	}

	for _, si := range shouldIgnore {
		if !Match(si) {
			t.Errorf("%q should be ignored", si)
		}
	}
	for _, sni := range shouldNotIgnore {
		if Match(sni) {
			t.Errorf("%q should not be ignored", sni)
		}
	}
}
