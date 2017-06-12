// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package index implements a basic index of packages and their relative
// installation states, as well as thier various top level metadata properties.
package index

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"

	"fuchsia.googlesource.com/pm/pkg"
)

// Index provides concurrency safe access to an index of packages and package metadata
type Index struct {
	root string
}

// New initializes an Index with the given root path. A directory at the
// given root will be created if it does not exist.
func New(root string) (*Index, error) {
	err := os.MkdirAll(root, os.ModePerm)
	if err != nil {
		return nil, err
	}
	return &Index{root: root}, nil
}

// List returns a list of all known packages in byte-lexical order.
func (idx *Index) List() ([]pkg.Package, error) {
	paths, err := filepath.Glob(idx.packagePath("*/*"))
	if err != nil {
		return nil, err
	}
	sort.Strings(paths)
	pkgs := make([]pkg.Package, len(paths))
	for i, path := range paths {
		pkgs[i].Version = filepath.Base(path)
		pkgs[i].Name = filepath.Base(filepath.Dir(path))
	}
	return pkgs, nil
}

// Add adds a package to the index
func (idx *Index) Add(p pkg.Package) error {
	if err := os.MkdirAll(idx.packagePath(p.Name), os.ModePerm); err != nil {
		return err
	}

	return ioutil.WriteFile(idx.packagePath(filepath.Join(p.Name, p.Version)), []byte{}, os.ModePerm)
}

// Remove removes a package from the index
func (idx *Index) Remove(p pkg.Package) error {
	return os.RemoveAll(idx.packageVersionPath(p.Name, p.Version))
}

func (idx *Index) packagePath(name string) string {
	return filepath.Join(idx.root, "packages", name)
}

func (idx *Index) packageVersionPath(name, version string) string {
	return filepath.Join(idx.root, "packages", name, version)
}
