// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package update contains the `pm update` command
package update

import (
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/merkle"
	"fuchsia.googlesource.com/pm/pkg"
)

// Run executes the `pm update` command
func Run(packageDir string) error {
	metadir := filepath.Join(packageDir, "meta")
	os.MkdirAll(metadir, os.ModePerm)

	f, err := os.Create(filepath.Join(metadir, "contents"))
	if err != nil {
		return err
	}
	defer f.Close()

	// TODO(raggi): instead of recreating the contents manifest with all found
	// files, just read the file and update the merkle roots for files in the
	// manifest
	return pkg.WalkContents(packageDir, func(path string) error {
		var t merkle.Tree
		cf, err := os.Open(filepath.Join(packageDir, path))
		if err != nil {
			return err
		}
		defer cf.Close()

		_, err = t.ReadFrom(cf)
		if err != nil {
			return err
		}

		_, err = fmt.Fprintf(f, "%s:%x\n", path, t.Root())
		if err != nil {
			return err
		}

		return nil
	})
}
