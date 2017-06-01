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
	index := &Index{root: root}
	if err := os.MkdirAll(index.NeedsBlobsDir(), os.ModePerm); err != nil {
		return nil, err
	}
	return index, nil
}

// List returns a list of all known packages in byte-lexical order.
func (idx *Index) List() ([]pkg.Package, error) {
	paths, err := filepath.Glob(idx.PackageVersionPath("*", "*"))
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
	if err := os.MkdirAll(idx.PackagePath(p.Name), os.ModePerm); err != nil {
		return err
	}

	return ioutil.WriteFile(idx.PackagePath(filepath.Join(p.Name, p.Version)), []byte{}, os.ModePerm)
}

// Remove removes a package from the index
func (idx *Index) Remove(p pkg.Package) error {
	return os.RemoveAll(idx.PackageVersionPath(p.Name, p.Version))
}

func (idx *Index) PackagePath(name string) string {
	return filepath.Join(idx.root, "packages", name)
}

func (idx *Index) PackageVersionPath(name, version string) string {
	return filepath.Join(idx.root, "packages", name, version)
}

// NeedsDir is the root of the needs directory
func (idx *Index) NeedsDir() string {
	return filepath.Join(idx.root, "needs")
}
func (idx *Index) InstallingDir() string {
	return filepath.Join(idx.root, "installing")
}
func (idx *Index) PackagesDir() string {
	return filepath.Join(idx.root, "packages")
}

// NeedsBlob provides the path to an index blob need, given a blob digest root
func (idx *Index) NeedsBlob(root string) string {
	return filepath.Join(idx.root, "needs", "blobs", root)
}

func (idx *Index) NeedsFile(name string) string {
	return filepath.Join(idx.root, "needs", name)
}

// NeedsBlobsDir provides the location of the index directory of needed blobs
func (idx *Index) NeedsBlobsDir() string {
	return filepath.Join(idx.root, "needs", "blobs")
}

func (idx *Index) WaitingDir() string {
	return filepath.Join(idx.root, "waiting")
}

func (idx *Index) WaitingPackageVersionPath(pkg, version string) string {
	return filepath.Join(idx.root, "waiting", pkg, version)
}

func (idx *Index) InstallingPackageVersionPath(pkg, version string) string {
	return filepath.Join(idx.root, "installing", pkg, version)
}
