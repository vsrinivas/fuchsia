// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"regexp"
	"sort"
	"strings"
)

// Snapshot contains metadata from one or more packages
type Snapshot struct {
	Packages map[string]Package      `json:"packages"`
	Blobs    map[MerkleRoot]BlobInfo `json:"blobs"`
}

// BlobInfo contains metadata about a single blob
type BlobInfo struct {
	Size uint64 `json:"size"`
}

// Package contains metadata about a package, including a list of all files in
// the package
type Package struct {
	Files map[string]MerkleRoot `json:"files"`
	Tags  []string              `json:"tags,omitempty"`
}

// String equality with basic wildcard support. "*" can match any number of
// characters. All other characters in each glob are matched literally. A
// string matches globs if it matches any glob.
func compileGlobs(globs []string) *regexp.Regexp {
	parts := []string{}
	for _, glob := range globs {
		globParts := []string{}
		for _, part := range strings.Split(glob, "*") {
			globParts = append(globParts, regexp.QuoteMeta(part))
		}
		parts = append(parts, strings.Join(globParts, ".*"))
	}

	s := fmt.Sprintf("^(%s)$", strings.Join(parts, "|"))

	return regexp.MustCompile(s)
}

func anyMatchRegexp(tags []string, re *regexp.Regexp) bool {
	for _, tag := range tags {
		if re.MatchString(tag) {
			return true
		}
	}
	return false
}

// LoadSnapshot reads and verifies a JSON formatted Snapshot from the provided
// path.
func LoadSnapshot(path string) (Snapshot, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return Snapshot{}, err
	}

	return ParseSnapshot(data)
}

// ParseSnapshot deserializes and verifies a JSON formatted Snapshot from the
// provided data.
func ParseSnapshot(jsonData []byte) (Snapshot, error) {
	var s Snapshot

	if err := json.Unmarshal(jsonData, &s); err != nil {
		return s, err
	}

	if err := s.Verify(); err != nil {
		return s, err
	}

	return s, nil
}

// AddPackage adds the metadata for a single package to the given package
// snapshot, detecting and reporting any inconsistencies with the provided
// metadata.
func (s *Snapshot) AddPackage(name string, blobs []PackageBlobInfo, tags []string) error {
	pkg := Package{
		make(map[string]MerkleRoot),
		tags,
	}

	newBlobs := make(map[MerkleRoot]BlobInfo)

	for _, blob := range blobs {
		pkg.Files[blob.Path] = blob.Merkle

		info := BlobInfo{blob.Size}
		if dup, ok := s.Blobs[blob.Merkle]; ok && dup != info {
			return fmt.Errorf("snapshot contains inconsistent blob metadata for %q (%v != %v)", blob.Merkle, dup, info)
		}
		newBlobs[blob.Merkle] = info
	}

	if dup, ok := s.Packages[name]; ok {
		allSame := true
		for name, merkle := range dup.Files {
			if pkg.Files[name] != merkle {
				allSame = false
				break
			}
		}
		if len(dup.Files) != len(pkg.Files) || !allSame {
			return fmt.Errorf("snapshot contains more than one package called %q (%v, %v)", name, dup, pkg)
		}
		allTags := make(map[string]struct{})
		for _, tag := range pkg.Tags {
			allTags[tag] = struct{}{}
		}
		for _, tag := range dup.Tags {
			allTags[tag] = struct{}{}
		}
		pkg.Tags = make([]string, 0, len(allTags))
		for tag := range allTags {
			pkg.Tags = append(pkg.Tags, tag)
		}
		sort.Strings(pkg.Tags)
	}

	s.Packages[name] = pkg
	for merkle, info := range newBlobs {
		s.Blobs[merkle] = info
	}

	return nil
}

// Verify determines if the snapshot is internally consistent. Specifically, it
// ensures that no package references a blob that does not have metadata and
// that the snapshot does not contain blob metadata that is not referenced by
// any package.
func (s *Snapshot) Verify() error {
	blobs := map[MerkleRoot]struct{}{}

	for name, pkg := range s.Packages {
		for path, merkle := range pkg.Files {
			blobs[merkle] = struct{}{}
			if _, ok := s.Blobs[merkle]; !ok {
				return fmt.Errorf("%s/%s references blob %v, but the blob does not exist", name, path, merkle)
			}
		}
	}

	for merkle := range s.Blobs {
		if _, ok := blobs[merkle]; !ok {
			return fmt.Errorf("snapshot contains blob %v, but no package references it", merkle)
		}
	}

	return nil
}

// Filter produces a subset of the Snapshot based on a series of include and
// exclude filters. In order for a package to be included in the result, its tags
// (including the package name itself) must match at least one include filter
// and must not match any exclude filters. Include and exclude filters may
// contain wildcard characters ("*"), which will match 0 or more characters.
func (s *Snapshot) Filter(include []string, exclude []string) Snapshot {
	res := Snapshot{
		Packages: map[string]Package{},
		Blobs:    map[MerkleRoot]BlobInfo{},
	}

	includeGlobs := compileGlobs(include)
	excludeGlobs := compileGlobs(exclude)

	for name, pkg := range s.Packages {
		tags := append(pkg.Tags, name)
		if anyMatchRegexp(tags, includeGlobs) && !anyMatchRegexp(tags, excludeGlobs) {
			res.Packages[name] = pkg
			for _, merkle := range pkg.Files {
				res.Blobs[merkle] = s.Blobs[merkle]
			}
		}
	}

	return res
}

// Size determines the storage required for all blobs in the snapshot (not
// including block alignment or filesystem overhead).
func (s *Snapshot) Size() uint64 {
	var res uint64
	for _, blob := range s.Blobs {
		res += blob.Size
	}
	return res
}
