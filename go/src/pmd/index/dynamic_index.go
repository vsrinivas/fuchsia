// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package index implements a basic index of packages and their relative
// installation states, as well as thier various top level metadata properties.
package index

import (
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"fuchsia.googlesource.com/pmd/amberer"

	"fuchsia.googlesource.com/pm/pkg"
)

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

	// installingCounts tracks the number of packages being installed which
	// have a dependecy on the given content id
	installingCounts map[string]uint

	// installingPkgs is a map from a meta FAR content ID of a package currently being installed
	// to all blobs needed by the package
	installingPkgs map[string][]string
}

// NewDynamic initializes an DynamicIndex with the given root path.
func NewDynamic(root string, static *StaticIndex) *DynamicIndex {
	// TODO(PKG-14): error is deliberately ignored. This should not be fatal to boot.
	_ = os.MkdirAll(root, os.ModePerm)
	return &DynamicIndex{
		root:             root,
		static:           static,
		installing:       make(map[string]pkg.Package),
		needs:            make(map[string]map[string]struct{}),
		waiting:          make(map[string]map[string]struct{}),
		installingCounts: make(map[string]uint),
		installingPkgs:   make(map[string][]string),
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
		idx.static.Set(p, root)

		idx.Notify(root)
		return nil
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

// TrackPkg starts tracking the package so that the index can notify watchers
// when it is fulfilled. blobStatus is a map from package blobs to a boolean
// which should be true if the blob already exists on the device and false
// if it is needed before the package can be considered fulfilled.
func (idx *DynamicIndex) TrackPkg(root string, p pkg.Package, blobStatus map[string]bool) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	idx.installing[root] = p

	// find the blobs that are part of the package which we don't have
	neededBlobs := make(map[string]struct{})
	for blob, has := range blobStatus {
		if has {
			continue
		}
		neededBlobs[blob] = struct{}{}
		if _, found := idx.needs[blob]; found {
			idx.needs[blob][root] = struct{}{}
		} else {
			idx.needs[blob] = map[string]struct{}{root: struct{}{}}
		}
	}
	idx.waiting[root] = neededBlobs

	// check if this package is already being installed
	if _, ok := idx.installingPkgs[root]; !ok {
		allBlobs := make([]string, 0, len(blobStatus)+1)
		allBlobs = append(allBlobs, root)
		idx.installingCounts[root]++
		for blob := range blobStatus {
			allBlobs = append(allBlobs, blob)
			idx.installingCounts[blob]++
		}
		idx.installingPkgs[root] = allBlobs
	}
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

			// remove reservations for all blobs needed by the fulfilled package
			if pkgContents, ok := idx.installingPkgs[pkgRoot]; ok {
				delete(idx.installingPkgs, pkgRoot)
				for _, con := range pkgContents {
					if c, ok := idx.installingCounts[con]; ok {
						c--
						if c == 0 {
							delete(idx.installingCounts, con)
						} else {
							idx.installingCounts[con] = c
						}
					}
				}
			}
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

// InstallingBlobs returns a list of all blobs IDs used by packages currently
// being installed. These are both the blobs the system already has and those
// still needed.
func (idx *DynamicIndex) InstallingBlobs() []string {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	neededBlobs := make([]string, 0, len(idx.installingCounts))
	for blob, _ := range idx.installingCounts {
		neededBlobs = append(neededBlobs, blob)
	}

	return neededBlobs
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

	go amberer.PackagesActivated(roots)
}

// PackageBlobs returns the list of blobs which are meta FARs backing packages in the index.
func (idx *DynamicIndex) PackageBlobs() ([]string, error) {
	paths, err := filepath.Glob(idx.PackageVersionPath("*", "*"))
	if err != nil {
		return nil, err
	}

	var errs []string
	blobIds := make([]string, 0, len(paths))
	for _, path := range paths {
		merkle, err := ioutil.ReadFile(path)
		if err != nil {
			errs = append(errs, err.Error())
		}
		blobIds = append(blobIds, string(merkle))
	}
	if len(errs) > 0 {
		err = fmt.Errorf("package index errors: %s", strings.Join(errs, ", "))
		log.Printf("%s", err)
	}
	return blobIds, err
}
