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

	"fuchsia.googlesource.com/pmd/amberer"

	"fuchsia.googlesource.com/pm/pkg"
)

// DynamicIndex provides concurrency safe access to a dynamic index of packages and package metadata
type DynamicIndex struct {
	root string

	static *StaticIndex

	// mu protects all following fields
	mu sync.Mutex

	// installing is a map of merkleroot -> package name/version
	installing map[string]pkg.Package

	// needs is a map of blob merkleroot -> set[package merkleroot] for packages that need blobs
	needs map[string]map[string]struct{}

	// waiting is a map of package merkleroot -> set[blob merkleroots]
	waiting map[string]map[string]struct{}
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

// Get looks up a package in the dynamic index, returning it if found.
func (idx *DynamicIndex) Get(p pkg.Package) (string, bool) {
	bmerkle, err := ioutil.ReadFile(idx.PackageVersionPath(p.Name, p.Version))
	return string(bmerkle), err == nil
}

// Add adds a package to the index
func (idx *DynamicIndex) Add(p pkg.Package, root string) error {
	if _, found := idx.static.GetRoot(root); found {
		return os.ErrExist
	}

	if _, found := idx.static.Get(p); found {
		// TODO(PKG-19): this needs to be removed as the static package set should not
		// be updated dynamically in future.
		err := idx.static.Set(p, root)

		idx.Notify(root)
		return err
	}

	if err := os.MkdirAll(idx.PackagePath(p.Name), os.ModePerm); err != nil {
		return err
	}

	path := idx.PackageVersionPath(p.Name, p.Version)
	if bmerkle, err := ioutil.ReadFile(path); err == nil && string(bmerkle) == root {
		return os.ErrExist
	}

	if err := ioutil.WriteFile(path, []byte(root), os.ModePerm); err != nil {
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
	// TODO(PKG-14): refactor out the initialization logic so that we can do this
	// once, at an appropriate point in the runtime.
	_ = os.MkdirAll(dir, os.ModePerm)
	return dir
}

// Installing marks the given package as being in the process of installing. The
// package identity is not yet known, and can be updated later using
// UpdateInstalling.
func (idx *DynamicIndex) Installing(root string) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	idx.installing[root] = pkg.Package{}
}

// UpdateInstalling updates the installing index for the given package with an
// identity once known (that is, once the package meta.far has been able to be
// opened, so the packages identity is known).
func (idx *DynamicIndex) UpdateInstalling(root string, p pkg.Package) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	idx.installing[root] = p
}

// InstallingFailed removes an entry from the package installation index, this
// is called when the package meta.far blob is not readable, or the package is
// not valid.
func (idx *DynamicIndex) InstallingFailed(root string) {
	log.Printf("installing package %s failed", root)

	idx.mu.Lock()
	defer idx.mu.Unlock()

	delete(idx.installing, root)
}

// AddNeeds updates the index about the blobs required in order to activate an
// installing package. It is possible for the addition of needs to race
// fulfillment that is happening in other concurrent processes. When that
// occurs, this method will return os.ErrExist.
func (idx *DynamicIndex) AddNeeds(root string, needs map[string]struct{}) error {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	if _, found := idx.installing[root]; !found {
		return os.ErrExist
	}

	for blob := range needs {
		if _, found := idx.needs[blob]; found {
			idx.needs[blob][root] = struct{}{}
		} else {
			idx.needs[blob] = map[string]struct{}{root: struct{}{}}
		}
	}
	// We wait on all of the "needs", that is, all blobs that were not found on the
	// system at the time of import.
	idx.waiting[root] = needs

	log.Printf("asking amber to fetch %d needed blobs", len(needs))
	go func() {
		for root := range needs {
			amberer.GetBlob(root)
		}
	}()
	return nil
}

// Fulfill processes the signal that a blob need has been fulfilled. meta.far's
// are also published through this path, but a meta.far fulfillment does not
// mean that the package is activated, only that its blob has been written. When
// a packages 'waiting' set has been emptied, fulfill will call Add, which is
// the point of activation.
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
			p := idx.installing[pkgRoot]
			if err := idx.Add(p, pkgRoot); err != nil {
				if os.IsExist(err) {
					log.Printf("package already exists at fulfillment: %s", err)
				} else {
					log.Printf("unexpected error adding package after fulfillment: %s", err)
				}
			} else {
				log.Printf("package activated %s/%s (%s)", p.Name, p.Version, pkgRoot)
			}
			delete(idx.installing, pkgRoot)
			fulfilled = append(fulfilled, pkgRoot)
		}
	}
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
	for name := range idx.needs {
		names = append(names, name)
	}

	return names
}

func (idx *DynamicIndex) Notify(roots ...string) {
	if len(roots) == 0 {
		return
	}

	go amberer.PackagesActivated(roots)
}

// PackageBlobs returns the list of blobs which are meta FARs backing packages
// in the dynamic and static indices.
func (idx *DynamicIndex) PackageBlobs() []string {
	packageBlobs := idx.static.PackageBlobs()
	paths, err := filepath.Glob(idx.PackageVersionPath("*", "*"))
	if err != nil {
		log.Printf("glob all extant dynamic packages: %s", err)
		return packageBlobs
	}

	for _, path := range paths {
		merkle, err := ioutil.ReadFile(path)
		if err != nil {
			log.Printf("read dynamic package index %s: %s", path, err)
			continue
		}
		packageBlobs = append(packageBlobs, string(merkle))
	}
	return packageBlobs
}

// AllPackageBlobs aggregates all installing, dynamic and static index package
// meta.far blobs into a single list. Any errors encountered along the way are
// logged, but otherwise the best available list is generated under a single
// lock, to provide a relatively consistent view of objects that must be
// maintained. This function is intended for use by the GC and the versions
// directory. The list will not contain duplicates.
func (idx *DynamicIndex) AllPackageBlobs() []string {
	allPackageBlobs := make(map[string]struct{})
	idx.mu.Lock()
	for blob := range idx.installing {
		allPackageBlobs[blob] = struct{}{}
	}
	idx.mu.Unlock()

	for _, blob := range idx.PackageBlobs() {
		allPackageBlobs[blob] = struct{}{}
	}

	blobList := make([]string, 0, len(allPackageBlobs))
	for blob := range allPackageBlobs {
		blobList = append(blobList, blob)
	}

	return blobList
}
