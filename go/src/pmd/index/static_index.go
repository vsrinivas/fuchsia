// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"bufio"
	"io"
	"log"
	"os"
	"sort"
	"strings"
	"sync"

	"fuchsia.googlesource.com/pm/pkg"
)

// StaticIndex is an index of packages that can not change. It is intended for
// use during early / verified boot stages to present a unified set of packages
// from a pre-computed and verifiable index file.
type StaticIndex struct {
	mu      sync.RWMutex
	roots   map[pkg.Package]string
	updates map[pkg.Package]string
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
	idx.updates = make(map[pkg.Package]string)
	idx.mu.Unlock()

	return nil
}

// HasName looks for a package with the given `name`
func (idx *StaticIndex) HasName(name string) bool {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	for k := range idx.updates {
		if k.Name == name {
			return true
		}
	}

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

	versions := map[string]struct{}{}

	for k := range idx.updates {
		if k.Name == name {
			versions[k.Version] = struct{}{}
		}
	}
	for k := range idx.roots {
		if k.Name == name {
			versions[k.Version] = struct{}{}
		}
	}

	verList := make([]string, 0, len(versions))
	for v := range versions {
		verList = append(verList, v)
	}

	return verList
}

// Get looks up the given package, returning (merkleroot, true) if found, or ("", false) otherwise.
func (idx *StaticIndex) Get(p pkg.Package) (string, bool) {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	s, ok := idx.updates[p]
	if ok {
		return s, ok
	}
	s, ok = idx.roots[p]
	return s, ok
}

// GetRoot looks for a package by merkleroot, returning the matching package and
// true, if found, an empty package and false otherwise.
func (idx *StaticIndex) GetRoot(root string) (pkg.Package, bool) {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	for p, rt := range idx.updates {
		if root == rt {
			return p, true
		}
	}

	for p, rt := range idx.roots {
		if root == rt {
			return p, true
		}
	}
	return pkg.Package{}, false
}

// Set sets the given package to the given root. TODO(PKG-16) This method should
// be removed in future, the static index should only be updated as a whole unit
// via Load.
func (idx *StaticIndex) Set(p pkg.Package, root string) error {
	idx.mu.Lock()
	defer idx.mu.Unlock()

	if idx.roots[p] == root || idx.updates[p] == root {
		return os.ErrExist
	}

	idx.updates[p] = root
	return nil
}

// List returns the list of packages in byte-lexical order
func (idx *StaticIndex) List() ([]pkg.Package, error) {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	var pkgs = make(map[pkg.Package]struct{})

	for k := range idx.updates {
		pkgs[k] = struct{}{}
	}
	for k := range idx.roots {
		pkgs[k] = struct{}{}
	}

	packages := make([]pkg.Package, 0, len(pkgs))
	for k := range pkgs {
		packages = append(packages, k)
	}
	sort.Sort(pkg.ByNameVersion(packages))
	return packages, nil
}

// PackageBlobs returns the list of blobs which are meta FARs backing packages in the index.
func (idx *StaticIndex) PackageBlobs() []string {

	var blbs = make(map[string]struct{})

	for _, m := range idx.updates {
		blbs[m] = struct{}{}
	}
	for _, m := range idx.roots {
		blbs[m] = struct{}{}
	}

	blobs := make([]string, 0, len(blbs))
	for m := range blbs {
		blobs = append(blobs, m)
	}
	return blobs
}
