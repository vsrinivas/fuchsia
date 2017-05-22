// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package testpackage has facilities for constructing packages that are useful
// for testing pm and associated code.
package testpackage

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
)

// Files is the list of files created by the default factories in this package.
var Files = []string{"a", "b", "dir/c"}

// New constructs a new test package with a default layout and construction. It
// contains 'a', 'b', and 'dir/c'. Each file contains it's name followed by a
// carriage return. The string returned is a path to a temporary directory
// containing the package files that the caller is responsible for removing.
func New() (string, error) {
	d, err := ioutil.TempDir("", "testpackage")
	if err != nil {
		return d, err
	}

	for _, name := range Files {
		path := filepath.Join(d, name)

		err = os.MkdirAll(filepath.Dir(path), os.ModePerm)
		if err != nil {
			return d, err
		}
		f, err := os.Create(path)
		if err != nil {
			return d, err
		}
		if _, err := fmt.Fprintf(f, "%s\n", path); err != nil {
			return d, err
		}
		err = f.Close()
		if err != nil {
			return d, err
		}
	}

	return d, err
}
