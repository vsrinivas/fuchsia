// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"path/filepath"
	"testing"
)

// NewFileTree(empty) should produce a filetree object that correctly
// represents an empty directory.
func TestFileTreeCreateEmpty(t *testing.T) {
	root, config := setupFileTreeTestDir("empty", t)

	got := NewFileTree(context.Background(), root, nil, config, NewMetrics())

	want := &FileTree{
		Name:  "empty",
		Path:  root,
		Files: []*File{},
	}

	if !got.Equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

// NewFileTree(simple) should produce a filetree object that correctly
// represents the simple testdata directory.
func TestFileTreeCreateSimple(t *testing.T) {
	root, config := setupFileTreeTestDir("simple", t)

	got := NewFileTree(context.Background(), root, nil, config, NewMetrics())

	f, err := NewFile(filepath.Join(root, "test.py"), got)
	if err != nil {
		t.Error(err)
	}
	want := &FileTree{
		Name:  "simple",
		Path:  root,
		Files: []*File{f},
	}

	if !got.Equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

// hasLowerPrefix must return true if the given filepath has a string prefix
// in the predefined list.
func TestFileTreeHasLowerPrefix(t *testing.T) {
	name := "LICENSE-THIRD-PARTY"
	singleLicenseFiles := []string{"license", "readme"}
	if !hasLowerPrefix(name, singleLicenseFiles) {
		t.Errorf("%v: %v is not a single license file", t.Name(), name)
	}
}

func setupFileTreeTestDir(name string, t *testing.T) (string, *Config) {
	configPath := filepath.Join(*testDataDir, "filetree", name+".json")
	baseDir, err := filepath.Abs(filepath.Join(*testDataDir, "filetree", name))
	if err != nil {
		t.Fatal(err)
	}

	config, err := NewConfig(configPath)
	if err != nil {
		t.Fatal(err)
	}
	return baseDir, config
}
