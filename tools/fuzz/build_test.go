// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bytes"
	"os"
	"os/exec"
	"testing"
)

// TODO(fxbug.dev/45425): somehow validate build layouts?

func newBaseBuild() *BaseBuild {
	return &BaseBuild{
		Fuzzers: make(map[string]*Fuzzer),
		Paths:   make(map[string]string),
	}
}

func TestLoadFuzzersFromMissingFile(t *testing.T) {
	build := newBaseBuild()

	filename := invalidPath
	build.Paths["fuzzers.json"] = filename
	if err := build.LoadFuzzers(); err == nil {
		t.Fatalf("expected failure for %q", filename)
	}
}

func TestLoadFuzzersFromInvalidFile(t *testing.T) {
	build := newBaseBuild()

	filename := createTempfileWithContents(t, "not valid json", "json")
	defer os.Remove(filename)

	build.Paths["fuzzers.json"] = filename
	if err := build.LoadFuzzers(); err == nil {
		t.Fatal("expected failure for invalid json")
	}
}

func TestLoadFuzzersFromEmptyFile(t *testing.T) {
	build := newBaseBuild()

	filename := createTempfileWithContents(t, "[]", "json")
	defer os.Remove(filename)

	build.Paths["fuzzers.json"] = filename

	if err := build.LoadFuzzers(); err != nil {
		t.Fatalf("error loading empty fuzzers: %s", err)
	}

	if len(build.Fuzzers) != 0 {
		t.Errorf("expected 0, got %d", len(build.Fuzzers))
	}
}

func TestLoadFuzzers(t *testing.T) {
	build := newBaseBuild()

	data := `[{"fuzzers_package": "foo", "fuzzers": ["bar", "baz"], "fuzz_host": false}]`
	filename := createTempfileWithContents(t, data, "json")
	defer os.Remove(filename)

	build.Paths["fuzzers.json"] = filename

	if err := build.LoadFuzzers(); err != nil {
		t.Fatalf("error loading fuzzers: %s", err)
	}

	if _, err := build.Fuzzer("foo/bar"); err != nil {
		t.Fatalf("missing expected fuzzer")
	}
	if _, err := build.Fuzzer("foo/baz"); err != nil {
		t.Fatalf("missing expected fuzzer")
	}
}

func TestPath(t *testing.T) {
	build := newBaseBuild()

	if _, err := build.Path(""); err == nil {
		t.Fatalf("expected failure for empty build")
	}
	if _, err := build.Path("foo"); err == nil {
		t.Fatalf("expected failure for empty build")
	}

	build.Paths["foo"] = "foo-value"
	val, err := build.Path("foo")
	if err != nil {
		t.Fatalf("expected success but got: %s", err)
	}
	if len(val) < 1 || val[0] != "foo-value" {
		t.Fatalf("expected foo-value, got %q", val)
	}

	build.Paths["bar"] = "bar-value"
	build.Paths["baz"] = "baz-value"
	build.Paths["foo"] = "foo-value"
	val, err = build.Path("foo")
	if err != nil {
		t.Fatalf("expected success but got: %s", err)
	}
	if len(val) < 1 || val[0] != "foo-value" {
		t.Fatalf("expected foo-value, got %q", val)
	}

	val, err = build.Path("bar", "baz")
	if err != nil {
		t.Fatalf("expected success but got: %s", err)
	}
	if len(val) < 2 || val[0] != "bar-value" || val[1] != "baz-value" {
		t.Fatalf("unexpected value, got %q", val)
	}
}

func TestSymbolize(t *testing.T) {
	// Enable subprocess mocking
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	build := newBaseBuild()
	build.Paths["symbolize"] = "symbolize"
	build.Paths["llvm-symbolizer"] = "llvm-symbolizer"

	// TODO(fxbug.dev/45425): more realistic test data
	inputData := "[1234.5][klog] INFO: {{{0x41}}}"
	expectedOutput := "wow.c:1\n"
	src := bytes.NewBufferString(inputData)
	var dst bytes.Buffer
	if err := build.Symbolize(src, &dst); err != nil {
		t.Fatalf("error during symbolization: %s", err)
	}

	if dst.String() != expectedOutput {
		t.Fatalf("unexpected symbolizer output: %q", dst.String())
	}
}

func TestStripLogPrefix(t *testing.T) {
	testCases := []string{
		"something",
		"[klog] INFO: something",
		"[1234][5678][9][klog] INFO: something",
		"[1234][klog] INFO: something",
		"[1234.5][klog] INFO: something",
	}

	want := "something"

	for _, input := range testCases {
		if got := stripLogPrefix(input); got != want {
			t.Fatalf("unexpected log prefix stripping result for %q: got %q, want %q",
				input, got, want)
		}
	}
}
