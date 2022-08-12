// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"bytes"
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestFileCreatedSuccessfully(t *testing.T) {
	setup(t)
	filename := filepath.Join(t.TempDir(), "success.txt")
	if err := ioutil.WriteFile(filename, []byte("Example Text"), 0600); err != nil {
		t.Fatal(err)
	}

	if _, err := NewFile(filename, SingleLicense); err != nil {
		t.Fatal(err)
	}
}

func TestFileCreationFails(t *testing.T) {
	setup(t)
	filename := filepath.Join(t.TempDir(), "failure.txt")

	if _, err := NewFile(filename, SingleLicense); err == nil {
		t.Fatal(err)
	}
}

func TestReplacements(t *testing.T) {
	setup(t)
	r := []*Replacement{
		{
			Replace: "“",
			With:    "\"",
		}, {
			Replace: "”",
			With:    "\"",
		},
	}
	Config.FuchsiaDir = "/"
	Config.Replacements = r
	expected := []byte("left quote: \" right quote: \"")

	filename := filepath.Join(t.TempDir(), "replacement.txt")
	if err := ioutil.WriteFile(filename, []byte("left quote: “ right quote: ”"), 0600); err != nil {
		t.Fatal(err)
	}

	f, err := NewFile(filename, SingleLicense)
	if err != nil {
		t.Fatal(err)
	}
	if len(f.Data) != 1 {
		t.Fatalf("Expected 1 data element, got %v\n", len(f.Data))
	}
	if !bytes.Equal(f.Data[0].Data, expected) {
		t.Fatalf("Expected %v, got %v\n", string(expected), string(f.Data[0].Data))
	}
}

func setup(t *testing.T) {
	Config = &FileConfig{
		FuchsiaDir: t.TempDir(),
	}
}
