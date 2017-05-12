// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package pkg contains the in memory representations of a Package in the pm
// system and associated utilities.
package pkg

import (
	"os"
	"path/filepath"
)

// Package is a representation of basic package metadata
type Package struct {
	Name    string `json:"name"`
	Version string `json:"version"`
	// TODO(raggi): there will be other things
}

// WalkContents is like a filepath.Walk in a package dir, but with a simplified
// interface
func WalkContents(d string, f func(path string) error) error {
	return filepath.Walk(d, func(abspath string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		path, err := filepath.Rel(d, abspath)
		if err != nil {
			return err
		}
		// TODO(raggi): needs some kind of ignorefile/config
		if path == "meta" || path == ".git" || path == ".jiri" {
			return filepath.SkipDir
		}
		if info.IsDir() {
			return nil
		}

		return f(path)
	})
}
