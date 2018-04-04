// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"bufio"
	"io"
	"log"
	"sort"
	"strings"
	"sync"

	"fuchsia.googlesource.com/pm/pkg"
)

// StaticIndex is an index of packages that can not change. It is intended for
// use during early / verified boot stages to present a unified set of packages
// from a pre-computed and verifiable index file.
type StaticIndex struct {
	mu    sync.RWMutex
	roots map[pkg.Package]string
}

// NewStatic initializes an empty StaticIndex
func NewStatic() *StaticIndex {
	return &StaticIndex{roots: map[pkg.Package]string{}}
}

// LoadFrom reads a static index from `path` and replaces the index in the
// receiver with the contents.
func (idx *StaticIndex) LoadFrom(f io.Reader) error {
	roots := map[pkg.Package]string{}

	r := bufio.NewReader(f)
	for {
		l, err := r.ReadString('\n')
		l = strings.TrimSpace(l)
		parts := strings.SplitN(l, "=", 2)

		if len(parts) == 2 {
			nameVersion := parts[0]
			merkle := parts[1]

			if len(merkle) != 64 {
				log.Printf("index: invalid merkleroot in static manifest: %q", l)
				goto checkErr
			}

			parts = strings.SplitN(nameVersion, "/", 2)
			if len(parts) != 2 {
				log.Printf("index: invalid name/version pair in static manifest: %q", nameVersion)
				goto checkErr
			}
			name := parts[0]
			version := parts[1]

			roots[pkg.Package{Name: name, Version: version}] = merkle
		} else {
			if len(l) > 0 {
				log.Printf("index: invalid line in static manifest: %q", l)
			}
		}

	checkErr:
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
	}

	idx.mu.Lock()
	idx.roots = roots
	idx.mu.Unlock()

	return nil
}

// HasName looks for a package with the given `name`
func (idx *StaticIndex) HasName(name string) bool {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	for k := range idx.roots {
		if k.Name == name {
			return true
		}
	}
	return false
}

// ListVersions returns the list of version strings given a package name
func (idx *StaticIndex) ListVersions(name string) []string {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	var versions []string
	for k := range idx.roots {
		if k.Name == name {
			versions = append(versions, k.Version)
		}
	}
	return versions
}

// Get looks up the given package, returning (merkleroot, true) if found, or ("", false) otherwise.
func (idx *StaticIndex) Get(p pkg.Package) (string, bool) {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	s, ok := idx.roots[p]
	return s, ok
}

// Set sets the given package to the given root. TODO(PKG-16) This method should
// be removed in future, the static index should only be updated as a whole unit
// via Load.
func (idx *StaticIndex) Set(p pkg.Package, root string) {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	idx.roots[p] = root
}

// List returns the list of packages in byte-lexical order
func (idx *StaticIndex) List() ([]pkg.Package, error) {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	packages := make([]pkg.Package, 0, len(idx.roots))
	for k := range idx.roots {
		packages = append(packages, k)
	}
	sort.Sort(pkg.ByNameVersion(packages))
	return packages, nil
}
