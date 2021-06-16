// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"testing"
)

func CheckLabelToDirectory(t *testing.T, input string, expected string) {
	actual, err := LabelToDirectory(input)
	if err != nil {
		t.Errorf("LabelToDirectory(\"%s\"): Expected \"%s\" got error \"%s\".", input, expected, err)
	}
	if actual != expected {
		t.Errorf("LabelToDirectory(\"%s\"): Expected \"%s\" got \"%s\".", input, expected, actual)
	}
}

func TestLabelToDirectory(t *testing.T) {
	CheckLabelToDirectory(t, "//abc", "abc")
	CheckLabelToDirectory(t, "//abc:label", "abc")
	CheckLabelToDirectory(t, "//abc/def", "abc/def")
	CheckLabelToDirectory(t, "//abc/def:label", "abc/def")
	CheckLabelToDirectory(t, "//abc/def:label(toolchain)", "abc/def")

	input := "no slashes"
	actual, err := LabelToDirectory(input)
	if err == nil {
		t.Errorf("LabelToDirectory(\"%s\"): Expected error (no leading slashes) got \"%s\".", input, actual)
	}
}
