// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// NewFile(path) should successfully return a file object for normal files.
func TestFileCreateNormal(t *testing.T) {
	tmpfile, err := ioutil.TempFile("", "normal.go")
	if err != nil {
		log.Fatal(err)
	}
	defer os.Remove(tmpfile.Name())

	_, err = NewFile(tmpfile.Name(), nil)
	if err != nil {
		t.Errorf("%v(): got %v, want %v", t.Name(), err, nil)
	}
}

// NewFile(path) should successfully return a file object for symlinked files.
func TestFileCreateRelativeSymlink(t *testing.T) {
	// File that we'll be pointing at.
	target, err := ioutil.TempFile("", "normal.go")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(target.Name())

	// Temporary directory for holding the symlink.
	tmpdir, err := ioutil.TempDir("", "symdir")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpdir)

	// Generate a relative path from tmpdir to target.
	rel, err := filepath.Rel(tmpdir, target.Name())
	if err != nil {
		t.Fatal(err)
	}

	// Create the symlink file in the temporary directory
	symlink := filepath.Join(tmpdir, "link")
	if err := os.Symlink(rel, symlink); err != nil {
		t.Fatal(err)
	}

	f, err := NewFile(symlink, nil)
	if err != nil {
		t.Errorf("%v(): got %v, want %v", t.Name(), err, nil)
	}
	if f.Path != symlink {
		t.Errorf("%v(): got %v, want %v", t.Name(), f.Path, symlink)
	}
	if f.Symlink != target.Name() {
		t.Errorf("%v(): got %v, want %v", t.Name(), f.Symlink, target.Name())
	}
}

// NewFile(path) should fail and return an error if the specified path
// does not exist.
func TestFileCreateNonExistent(t *testing.T) {
	// Temporary directory for holding the symlink.
	tmpdir, err := ioutil.TempDir("", "symdir")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpdir) // clean up

	notExist := filepath.Join(tmpdir, "DoesNotExist.txt")
	f, err := NewFile(notExist, nil)

	expectedErrorMsg := fmt.Sprintf("lstat %v: no such file or directory", notExist)
	if err == nil || !strings.Contains(err.Error(), expectedErrorMsg) {
		t.Errorf("%v(): got %v, want %v", t.Name(), err, expectedErrorMsg)
	}
	if f != nil {
		t.Errorf("%v(): got %v, want %v", t.Name(), f, nil)
	}
}
