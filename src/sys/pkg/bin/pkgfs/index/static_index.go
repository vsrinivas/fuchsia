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

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
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

type indexFileEntry struct {
	Key    pkg.Package
	Merkle string
}

// ParseIndexFile parses the key=value format in static_packages.
func ParseIndexFile(f io.Reader) (result []indexFileEntry, _ error) {
	r := bufio.NewReader(f)
	for {
		l, err := r.ReadString('\n')
		l = strings.TrimSpace(l)
		if err != nil {
			if err == io.EOF {
				if l == "" {
					// We're done
					break
				} else {
					// Keep going for one more record
				}
			} else {
				return nil, err
			}
		}
		parts := strings.SplitN(l, "=", 2)

		if len(parts) == 2 {
			nameVersion := parts[0]
			merkle := parts[1]

			if len(merkle) != 64 {
				log.Printf("index: invalid merkleroot in static manifest: %q", l)
				continue
			}

			parts = strings.SplitN(nameVersion, "/", 2)
			if len(parts) != 2 {
				log.Printf("index: invalid name/version pair in static manifest: %q", nameVersion)
				continue
			}
			name := parts[0]
			version := parts[1]

			result = append(result, indexFileEntry{Key: pkg.Package{Name: name, Version: version}, Merkle: merkle})
		} else {
			if len(l) > 0 {
				log.Printf("index: invalid line in static manifest: %q", l)
			}
		}
	}
	return
}

// LoadFrom reads a static index from `path` and replaces the index in the
// receiver with the contents.
func (idx *StaticIndex) LoadFrom(f io.Reader, systemImage pkg.Package, systemImageMerkleRoot string) error {
	roots := map[pkg.Package]string{}

	entries, err := ParseIndexFile(f)
	if err != nil {
		return err
	}
	for _, entry := range entries {
		roots[entry.Key] = entry.Merkle
	}

	roots[systemImage] = systemImageMerkleRoot

	idx.mu.Lock()
	idx.roots = roots
	idx.updates = make(map[pkg.Package]string)
	idx.mu.Unlock()

	return nil
}

// HasStaticName looks for a package with the given `name` in the static static
// index, ignoring any runtime updates made to the static index.
func (idx *StaticIndex) HasStaticName(name string) bool {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	for k := range idx.roots {
		if k.Name == name {
			return true
		}
	}
	return false
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
	s, ok := idx.roots[p]
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

// HasStaticRoot looks for a package by merkleroot in the static static index,
// ignoring any runtime updates made to the static index.
func (idx *StaticIndex) HasStaticRoot(root string) bool {
	idx.mu.RLock()
	defer idx.mu.RUnlock()

	for _, rt := range idx.roots {
		if root == rt {
			return true
		}
	}
	return false
}

// Set sets the given package to the given root. TODO(fxbug.dev/21988) This method should
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

// StaticPacakgeBlobs returns the blobs that are the meta FARs for the packages
// in the static index and never changes, unlike PackageBlobs() which will also
// include updated versions of packages in the index.
func (idx *StaticIndex) StaticPackageBlobs() []string {
	b := make([]string, 0, len(idx.roots))
	for _, m := range idx.roots {
		b = append(b, m)
	}
	return b
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
