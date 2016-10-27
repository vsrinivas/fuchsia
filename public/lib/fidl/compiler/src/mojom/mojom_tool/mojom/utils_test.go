// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRelPathIfShorter(t *testing.T) {
	cwd, err := os.Getwd()
	if err != nil {
		t.Fatalf(err.Error())
	}

	// Test RelPathIfShorter with an absolute path to a directory
	// contained in the current working directory.
	fullPath, err := filepath.Abs(filepath.Join(cwd, "a", "b", "c"))
	if err != nil {
		t.Fatalf(err.Error())
	}
	result := RelPathIfShorter(fullPath)
	expected := filepath.Join("a", "b", "c")
	if result != expected {
		t.Errorf("%s != %s", expected, result)
	}

	// Test RelPathIfShorter with an absolute path to a directory
	// not contained in the current working directory.
	fullPath = "/foo/bar/baz"
	result = RelPathIfShorter(fullPath)
	expected = fullPath
	if result != expected {
		t.Errorf("%s != %s", expected, result)
	}

}
