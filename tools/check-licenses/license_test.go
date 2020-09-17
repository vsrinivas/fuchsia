// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"regexp"
	"testing"
)

func TestLicenseAppend(t *testing.T) {
	license := License{
		pattern:  regexp.MustCompile("abcdefghijklmnopqrs\ntuvwxyz"),
		category: "alphabet-test",
	}
	want := 0
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches), want)
	}
	license.append("test_path_0")
	want = 1
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches), want)
	}
	if len(license.matches[""].files) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches[""].files), want)
	}
}
