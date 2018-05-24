// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package index implements a basic index of packages and their relative
// installation states, as well as thier various top level metadata properties.
package index

import (
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"sort"
	"sync"

	"fuchsia.googlesource.com/pm/pkg"
)

// PackageActivationNotifer is a slice of the Amber interface that notifies Amber of package activations.
type PackageActivationNotifier interface {
	PackagesActivated([]string) error
}

// DynamicIndex provides concurrency safe access to a dynamic index of packages and package metadata
type DynamicIndex struct {
	root string

	// TODO(PKG-19): this will be removed in future
	static *StaticIndex

	// mu protects all following fields
	mu sync.Mutex

	// installing is a map of merkleroot -> package name/version
	installing map[string]pkg.Package

	// needs is a map of blob merkleroot -> set[package merkleroot] for packages that need blobs
	needs map[string]map[string]struct{}

	// waiting is a map of package merkleroot -> set[blob merkleroots]
	waiting map[string]map[string]struct{}

	// Notifier is used to send activation notifications to Amber, if set.
	Notifier PackageActivationNotifier
}

// NewDynamic initializes an DynamicIndex with the given root path.
func NewDynamic(root string, static *StaticIndex) *DynamicIndex {
	// TODO(PKG-14): error is deliberately ignored. This should not be fatal to boot.
	_ = os.MkdirAll(root, os.ModePerm)
	return &DynamicIndex{
		root:       root,
		static:     static,
		installing: make(map[string]pkg.Package),
		needs:      make(map[string]map[string]struct{}),
		waiting:    make(map[string]map[string]struct{}),
	}
}

// List returns a list of all known packages in byte-lexical order.
func (idx *DynamicIndex) List() ([]pkg.Package, error) {
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
func (idx *DynamicIndex) Add(p pkg.Package, root string) error {
	if err := os.MkdirAll(idx.PackagePath(p.Name), os.ModePerm); err != nil {
		return err
	}

	// TODO(PKG-19): this needs to be removed as the static package set should not
	// be updated dynamically in future.
	idx.static.Set(p, root)

	if err := ioutil.WriteFile(idx.PackagePath(filepath.Join(p.Name, p.Version)), []byte(root), os.ModePerm); err != nil {
		return err
	}
	idx.Notify(root)
	return nil
}

func (idx *DynamicIndex) PackagePath(name string) string {
	return filepath.Join(idx.PackagesDir(), name)
}

func (idx *DynamicIndex) PackageVersionPath(name, version string) string {
	return filepath.Join(idx.PackagesDir(), name, version)
}

func (idx *DynamicIndex) PackagesDir() string {
	dir := filepath.Join(idx.root, "packages")
	// TODO(PKG-14): refactor out the initialization logic so that we can do this once, at an appropriate point in the runtime.
	_ = os.MkdirAll(dir, os.ModePerm)
	return dir
}

func (idx *DynamicIndex) AddNeeds(root string, p pkg.Package, blobs map[string]struct{}) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	idx.installing[root] = p
	for blob := range blobs {
		if _, found := idx.needs[blob]; found {
			idx.needs[blob][root] = struct{}{}
		} else {
			idx.needs[blob] = map[string]struct{}{root: struct{}{}}
		}
	}
	idx.waiting[root] = blobs
}

func (idx *DynamicIndex) Fulfill(need string) []string {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	fulfilled := []string{}
	packageRoots := idx.needs[need]
	delete(idx.needs, need)
	for pkgRoot := range packageRoots {
		waiting := idx.waiting[pkgRoot]
		delete(waiting, need)
		if len(waiting) == 0 {
			delete(idx.waiting, pkgRoot)
			idx.Add(idx.installing[pkgRoot], pkgRoot)
			delete(idx.installing, pkgRoot)
			fulfilled = append(fulfilled, pkgRoot)
			continue
		}
	}
	idx.Notify(fulfilled...)
	return fulfilled
}

func (idx *DynamicIndex) HasNeed(root string) bool {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	_, found := idx.needs[root]
	return found
}

func (idx *DynamicIndex) NeedsList() []string {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	names := make([]string, 0, len(idx.needs))
	for name, _ := range idx.needs {
		names = append(names, name)
	}

	return names
}

func (idx *DynamicIndex) Notify(roots ...string) {
	if len(roots) == 0 {
		return
	}

	if idx.Notifier != nil {
		go func() {
			if err := idx.Notifier.PackagesActivated(roots); err != nil {
				log.Printf("pkgfs: index: Amber notification error: %s", err)
			}
		}()
	}
}
