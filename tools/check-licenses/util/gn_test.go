// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"flag"
	"testing"
)

var (
	gnPath   = flag.String("gn_path", "", "Path to gn executable")
	buildDir = flag.String("build_dir", "", "Path to out directory")
)

func CheckLabelToDirectory(t *testing.T, gn *Gn, input string, expected string) {
	actual := gn.labelToDirectory(input)
	if len(actual) < 1 {
		t.Errorf("LabelToDirectory(\"%s\"): Expected at least 1 entry, got 0.", input)
	}
	if actual[0] != expected {
		t.Errorf("LabelToDirectory(\"%s\"): Expected \"%s\" got \"%s\".", input, expected, actual)
	}
}

func TestLabelToDirectory(t *testing.T) {
	gn, err := NewGn(*gnPath, *buildDir)
	if err != nil {
		t.Errorf("LabelToDirectory(\"%s\"): Error creating GN object.", err)
	}

	CheckLabelToDirectory(t, gn, "//abc", "//abc")
	CheckLabelToDirectory(t, gn, "//abc:label", "//abc")
	CheckLabelToDirectory(t, gn, "//abc/def", "//abc/def")
	CheckLabelToDirectory(t, gn, "//abc/def:label", "//abc/def")
	CheckLabelToDirectory(t, gn, "//abc/def:label(toolchain)", "//abc/def")
}
