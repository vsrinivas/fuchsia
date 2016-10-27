// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"testing"
)

func testOutputFileByFilePath(t *testing.T) {
	config := generatorCliConfig{
		outputDir:   "/some/output/path/",
		srcRootPath: "/alpha/",
	}
	fileName := "/alpha/beta/gamma/file.d"

	expected := "/some/output/path/beta/gamma/file.d"

	actual := outputFileByFilePath(fileName, config)
	if actual != expected {
		t.Fatalf("Expected: %q\nActual: %q", expected, actual)
	}
}

func testChangeExt(t *testing.T) {
	fileName := "/alpha/beta/gamma/file.mojom"
	expected := "/alpha/beta/gamma/file.d"
	actual := changeExt(fileName, ".d")

	if actual != expected {
		t.Fatalf("Expected: %q\nActual: %q", expected, actual)
	}
}

func testChangeExtNoExt(t *testing.T) {
	fileName := "/alpha/beta/gamma/file"
	expected := "/alpha/beta/gamma/file.d"
	actual := changeExt(fileName, ".d")

	if actual != expected {
		t.Fatalf("Expected: %q\nActual: %q", expected, actual)
	}
}
