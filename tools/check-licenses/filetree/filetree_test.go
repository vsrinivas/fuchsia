// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

import (
	"context"
	"encoding/json"
	"flag"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "", "Path to test data directory")

// NewFileTree(empty) should produce a filetree object that correctly
// represents an empty directory.
func TestFileTreeCreateEmpty(t *testing.T) {
	want, got := setupFileTreeTestDir("empty", t)
	if got != want {
		t.Errorf("%v(): got \n%v\n, want \n%v\n", t.Name(), got, want)
	}
}

// NewFileTree(simple) should produce a filetree object that correctly
// represents the simple testdata directory.
func TestFileTreeCreateSimple(t *testing.T) {
	want, got := setupFileTreeTestDir("simple", t)
	if got != want {
		t.Errorf("%v(): got \n%v\n, want \n%v\n", t.Name(), got, want)
	}
}

// NewFileTree(skip) should produce a filetree object that correctly
// skips the configured directories.
func TestFileTreeWithSkips(t *testing.T) {
	want, got := setupFileTreeTestDir("skipdir", t)
	if got != want {
		t.Errorf("%v(): got \n%v\n, want \n%v\n", t.Name(), got, want)
	}
}

func setupFileTreeTestDir(name string, t *testing.T) (string, string) {
	// Find the right testdata directory for this test.
	testDir := filepath.Join(*testDataDir, name)
	root := filepath.Join(testDir, "root")

	// Create a FileTree object from the want.json file.
	want, err := os.ReadFile(filepath.Join(testDir, "want.json"))
	if err != nil {
		t.Fatal(err)
	}
	wantString := strings.Replace(string(want), "{root}", root, -1)
	want = []byte(wantString)

	// Load the accompanying config file for this test type.
	configJson, err := os.ReadFile(filepath.Join(testDir, "config.json"))
	if err != nil {
		t.Fatal(err)
	}
	configString := strings.Replace(string(configJson), "{root}", root, -1)
	configJson = []byte(configString)

	// Unmarshal the config json data into the Config object, and run
	// NewFileTree.
	if err = json.Unmarshal(configJson, Config); err != nil {
		t.Fatal(err)
	}

	gotTree, err := NewFileTree(context.Background(), root, nil)
	if err != nil {
		t.Fatal(err)
	}

	// Return the results of NewFileTree and the expected json text
	// for direct comparison.
	got, err := json.MarshalIndent(gotTree, "", "    ")
	if err != nil {
		t.Fatal(err)
	}

	return strings.TrimSpace(string(want)), strings.TrimSpace(string(got))
}
