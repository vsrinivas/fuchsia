// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package directory

import (
	"encoding/json"
	"flag"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "", "Path to test data directory")

// NewDirectory(empty) should produce a directory object that correctly
// represents an empty directory.
func TestDirectoryCreateEmpty(t *testing.T) {
	want, got := setupDirectoryTestDir("empty", t)
	if got != want {
		t.Errorf("%v(): got \n%v\n, want \n%v\n", t.Name(), got, want)
	}
}

// NewDirectory(simple) should produce a directory object that correctly
// represents the simple testdata directory.
func TestDirectoryCreateSimple(t *testing.T) {
	want, got := setupDirectoryTestDir("simple", t)
	if got != want {
		t.Errorf("%v(): got \n%v\n, want \n%v\n", t.Name(), got, want)
	}
}

// NewDirectory(skip) should produce a directory object that correctly
// skips the configured directories.
func TestDirectoryWithSkips(t *testing.T) {
	want, got := setupDirectoryTestDir("skipdir", t)
	if got != want {
		t.Errorf("%v(): got \n%v\n, want \n%v\n", t.Name(), got, want)
	}
}

func setupDirectoryTestDir(name string, t *testing.T) (string, string) {
	// Find the right testdata directory for this test.
	testDir := filepath.Join(*testDataDir, name)
	root := filepath.Join(testDir, "root")

	// Create a Directory object from the want.json file.
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
	// NewDirectory.
	c := NewConfig()
	if err = json.Unmarshal(configJson, c); err != nil {
		t.Fatal(err)
	}
	Config = c

	gotTree, err := NewDirectory(root, nil)
	if err != nil {
		t.Fatal(err)
	}

	// Return the results of NewDirectory and the expected json text
	// for direct comparison.
	got, err := json.MarshalIndent(gotTree, "", "    ")
	if err != nil {
		t.Fatal(err)
	}

	return strings.TrimSpace(string(want)), strings.TrimSpace(string(got))
}
