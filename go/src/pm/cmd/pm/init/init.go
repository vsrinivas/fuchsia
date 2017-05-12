// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package init contains the `pm init` command
package init

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/pkg"
)

// Run initializes package metadata in the given package directory. A manifest
// is generated with a name matching the directory name. A content manifest is
// also created including all files found in the package directory.
func Run(packageDir string) error {
	pkgName := filepath.Base(packageDir)
	metadir := filepath.Join(packageDir, "meta")
	os.MkdirAll(metadir, os.ModePerm)

	meta := filepath.Join(metadir, "package.json")
	if _, err := os.Stat(meta); os.IsNotExist(err) {
		f, err := os.Create(meta)
		if err != nil {
			return err
		}

		p := pkg.Package{
			Name:    pkgName,
			Version: "0",
		}

		err = json.NewEncoder(f).Encode(&p)
		f.Close()
		if err != nil {
			return err
		}
	}

	f, err := os.Create(filepath.Join(metadir, "contents"))
	if err != nil {
		return err
	}
	defer f.Close()

	return pkg.WalkContents(packageDir, func(path string) error {
		_, err := fmt.Fprintln(f, path)
		return err
	})
}
