// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"encoding/json"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"
)

// NewFileTree(empty) should produce a filetree object that correctly
// represents an empty directory.
func TestFileTreeCreateEmpty(t *testing.T) {
	want, got := setupFileTreeTestDir("empty", t)
	if !got.equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

// NewFileTree(simple) should produce a filetree object that correctly
// represents the simple testdata directory.
func TestFileTreeCreateSimple(t *testing.T) {
	want, got := setupFileTreeTestDir("simple", t)
	if !got.equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

// StrictAnalysis should be initially set by the config file,
// and propagated down from the parent tree properly.
func TestFileTreeStrictAnalysis(t *testing.T) {
	want, got := setupFileTreeTestDir("strictanalysis", t)
	if !got.equal(want) {
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

func TestFileTreeWithDontSkip(t *testing.T) {
	want, got := setupFileTreeTestDir("skipdir", t)
	if !got.equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

func loadFileAndReplace(path string, replacements map[string]string) (string, error) {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		return "", err
	}
	text := string(b)

	for k, v := range replacements {
		text = strings.ReplaceAll(text, k, v)
	}
	return text, nil
}

func setupFileTreeTestDir(name string, t *testing.T) (*FileTree, *FileTree) {
	// Find the right testdata directory for this test.
	testDir, err := filepath.Abs(filepath.Join(*testDataDir, "filetree", name))
	if err != nil {
		t.Fatal(err)
	}

	// The filetree will be called on the subdirectory named "root".
	root := filepath.Join(testDir, "root")

	// want.json contains variables that need to be replaced before it can be used.
	replacements := map[string]string{
		"{root}": root,
	}

	path := filepath.Join(testDir, "want.json")
	wantJson, err := loadFileAndReplace(path, replacements)
	if err != nil {
		t.Fatal(err)
	}

	// Create a FileTree object from the want.json file.
	d := json.NewDecoder(strings.NewReader(wantJson))
	d.DisallowUnknownFields()
	var want FileTree
	if err := d.Decode(&want); err != nil {
		t.Fatal(err)
	}

	// Load the accompanying config file for this test type.
	path = filepath.Join(testDir, "config.json")
	configJson, err := loadFileAndReplace(path, replacements)
	if err != nil {
		t.Fatal(err)
	}

	config, err := NewConfigJson(configJson)
	if err != nil {
		t.Fatal(err)
	}

	got, err := NewFileTree(context.Background(), root, nil, config, NewMetrics())
	if err != nil {
		t.Fatal(err)
	}

	return &want, got
}

func (ft *FileTree) equal(other *FileTree) bool {
	if ft.Name != other.Name {
		return false
	}
	if ft.Path != other.Path {
		return false
	}
	if ft.StrictAnalysis != other.StrictAnalysis {
		return false
	}

	if len(ft.SingleLicenseFiles) != len(other.SingleLicenseFiles) {
		return false
	}
	for k := range ft.SingleLicenseFiles {
		left := ft.SingleLicenseFiles[k]
		right := other.SingleLicenseFiles[k]
		if len(left) != len(right) {
			return false
		}
		for i := range left {
			if left[i] != right[i] {
				return false
			}
		}
	}

	if len(ft.Files) != len(other.Files) {
		return false
	}
	for i := range ft.Files {
		if !ft.Files[i].equal(other.Files[i]) {
			return false
		}
	}

	if len(ft.Children) != len(other.Children) {
		return false
	}
	for k := range ft.Children {
		if ft.Children[k].equal(other.Children[k]) {
			return false
		}
	}

	return true
}
