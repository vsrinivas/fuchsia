// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkg

import (
	"os"
	"path/filepath"
	"testing"

	"fuchsia.googlesource.com/pm/testpackage"
)

func TestWalkContents(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	ignoredFiles := []string{
		"meta/signature",
		"meta/contents",
		".git/HEAD",
		".jiri/manifest",
	}

	for _, f := range ignoredFiles {
		touch(filepath.Join(d, f))
	}
	touch(filepath.Join(d, "meta/package.json"))

	found := map[string]struct{}{}
	err = WalkContents(d, func(path string) error {
		found[path] = struct{}{}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}

	if len(found) != 4 {
		t.Errorf("unexpected number of files walked. Found: %#v", found)
	}

	for _, f := range testpackage.Files {
		if _, ok := found[f]; !ok {
			t.Errorf("package file %q was not found", f)
		}
	}
	if _, ok := found["meta/package.json"]; !ok {
		t.Errorf("expected to find %s", "meta/package.json")
	}

	for _, f := range ignoredFiles {
		if _, ok := found[f]; ok {
			t.Errorf("walk contents did not ignore file %q", f)
		}
	}
}

// touch creates a file at the given path, it panics on error
func touch(path string) {
	if err := os.MkdirAll(filepath.Dir(path), os.ModePerm); err != nil {
		panic(err)
	}
	f, err := os.Create(path)
	if err != nil {
		panic(err)
	}
	if err := f.Close(); err != nil {
		panic(err)
	}
}
