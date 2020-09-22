// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package index implements a basic index of packages and their relative
// installation states, as well as thier various top level metadata properties.
package index

import (
	"log"
	"os"
	"sync"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
)

// DynamicIndex provides concurrency safe access to a dynamic index of packages and package metadata
type DynamicIndex struct {
	static *StaticIndex

	// mu protects all following fields
	mu sync.Mutex

	// roots is a map of merkleroot -> package name/version for active packages
	roots map[string]pkg.Package

	// index is a map of package name/version -> most recently activated merkleroot
	index map[pkg.Package]string

	// indexOrdered contains the list of keys in index in initial insertion order
	//
	// Used to make the List() order deterministic.
	indexOrdered []pkg.Package

	// installing is a map of merkleroot -> package name/version
	installing map[string]pkg.Package

	// needs is a map of blob merkleroot -> set[package merkleroot] for packages that need blobs
	needs map[string]map[string]struct{}

	// waiting is a map of package merkleroot -> set[blob merkleroots]
	waiting map[string]map[string]struct{}
}

// NewDynamic initializes a DynamicIndex
func NewDynamic(static *StaticIndex) *DynamicIndex {
	return &DynamicIndex{
		static:       static,
		roots:        make(map[string]pkg.Package),
		index:        make(map[pkg.Package]string),
		indexOrdered: nil,
		installing:   make(map[string]pkg.Package),
		needs:        make(map[string]map[string]struct{}),
		waiting:      make(map[string]map[string]struct{}),
	}
}

// Get looks up a package in the dynamic index, returning it if found.
func (idx *DynamicIndex) Get(p pkg.Package) (result string, found bool) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	result, found = idx.index[p]
	return
}

// List lists every package in the dynamic index in insertion order.
func (idx *DynamicIndex) List() []pkg.Package {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	pkgs := make([]pkg.Package, len(idx.index))
	copy(pkgs, idx.indexOrdered)
	return pkgs
}

// Add adds a package to the index
func (idx *DynamicIndex) Add(p pkg.Package, root string) error {
	idx.mu.Lock()
	defer idx.mu.Unlock()
	return idx.addLocked(p, root)
}

func (idx *DynamicIndex) addLocked(p pkg.Package, root string) error {
	// After being added, a package is not in an installing state.
	delete(idx.installing, root)

	// Defensive: while we must assume caller-correctness, so these should never
	// match, if we ever are called, the only safe thing to do is to cleanup state
	// as best we can, lest external API is littered with leaks from our internal
	// tracking, which has non-local side effects.
	delete(idx.waiting, root)
	for _, needs := range idx.needs {
		delete(needs, root)
	}

	if _, found := idx.static.GetRoot(root); found {
		return os.ErrExist
	}

	if _, found := idx.static.Get(p); found {
		// TODO(fxbug.dev/21991): this needs to be removed as the static package set should not
		// be updated dynamically in future.
		err := idx.static.Set(p, root)

		return err
	}

	if oldRoot, ok := idx.index[p]; ok {
		delete(idx.roots, oldRoot)
	} else {
		idx.indexOrdered = append(idx.indexOrdered, p)
	}
	idx.index[p] = root
	idx.roots[root] = p
	return nil
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

// InstallingFailedForPackage removes an entry from the package installation index,
// this is called when the package meta.far blob is not readable, or the package is
// not valid.
func (idx *DynamicIndex) InstallingFailedForPackage(pkgRoot string) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	p := idx.installing[pkgRoot]
	log.Printf("package failed %s/%s (%s)", p.Name, p.Version, pkgRoot)
	delete(idx.installing, pkgRoot)
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
			idx.needs[blob] = map[string]struct{}{root: {}}
		}
	}
	// We wait on all of the "needs", that is, all blobs that were not found on the
	// system at the time of import.
	idx.waiting[root] = needs
	return nil
}

// Fulfill processes the signal that a blob need has been fulfilled. meta.far's
// are also published through this path, but a meta.far fulfillment does not
// mean that the package is activated, only that its blob has been written. When
// a packages 'waiting' set has been emptied, fulfill will call Add, which is
// the point of activation.
func (idx *DynamicIndex) Fulfill(need string) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	packageRoots := idx.needs[need]
	delete(idx.needs, need)

	for pkgRoot := range packageRoots {
		waiting := idx.waiting[pkgRoot]
		delete(waiting, need)
		if len(waiting) == 0 {
			delete(idx.waiting, pkgRoot)
			p := idx.installing[pkgRoot]
			if err := idx.addLocked(p, pkgRoot); err != nil {
				if os.IsExist(err) {
					log.Printf("package already exists at fulfillment: %s", err)
				} else {
					log.Printf("unexpected error adding package after fulfillment: %s", err)
				}
			} else {
				log.Printf("cached %s/%s (%s)", p.Name, p.Version, pkgRoot)
			}
		}
	}
}

func (idx *DynamicIndex) PkgHasNeed(pkg, root string) bool {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	// TODO(computerdruid): replace this with logic that uses idx.waiting and delete idx.needs
	needs, found := idx.needs[pkg]
	if !found {
		return found
	}
	for need := range needs {
		if need == root {
			return true
		}
	}
	return false
}

func (idx *DynamicIndex) PkgNeedsList(pkgRoot string) []string {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	pkgNeeds, found := idx.waiting[pkgRoot]
	if !found {
		return []string{}
	}
	blobs := make([]string, 0, len(pkgNeeds))
	for blob := range pkgNeeds {
		blobs = append(blobs, blob)
	}
	return blobs
}

func (idx *DynamicIndex) InstallingList() []string {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	names := make([]string, 0, len(idx.installing))
	for name := range idx.installing {
		names = append(names, name)
	}
	return names
}

func (idx *DynamicIndex) IsInstalling(merkle string) bool {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	_, found := idx.installing[merkle]
	return found
}

// GetRoot looks for a package by merkleroot, returning the matching package and
// true, if found, an empty package and false otherwise.
func (idx *DynamicIndex) GetRoot(root string) (pkg.Package, bool) {
	p, found := idx.static.GetRoot(root)
	if found {
		return p, found
	}

	idx.mu.Lock()
	defer idx.mu.Unlock()
	p, found = idx.roots[root]
	return p, found
}

// PackageBlobs returns the list of blobs which are meta FARs backing packages
// in the dynamic and static indices.
func (idx *DynamicIndex) PackageBlobs() []string {
	packageBlobs := idx.static.PackageBlobs()
	idx.mu.Lock()
	dynamicBlobs := make([]string, 0, len(idx.roots))
	for merkle := range idx.roots {
		dynamicBlobs = append(dynamicBlobs, string(merkle))
	}
	idx.mu.Unlock()

	return append(packageBlobs, dynamicBlobs...)
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
