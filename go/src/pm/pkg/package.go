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
}

// WalkContents is like a filepath.Walk in a package dir, but with a simplified
// interface. It skips over the meta/signature and meta/contents files, which
// are not able to be included in the contents file itself.
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
		switch path {
		case "meta/signature", "meta/contents":
			return nil
		case ".git", ".jiri":
			return filepath.SkipDir
		}
		if info.IsDir() {
			return nil
		}

		return f(path)
	})
}
