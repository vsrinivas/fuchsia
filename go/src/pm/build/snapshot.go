// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
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

// Size determines the storage required for all blobs in the snapshot (not
// including block alignment or filesystem overhead).
func (s *Snapshot) Size() uint64 {
	var res uint64
	for _, blob := range s.Blobs {
		res += blob.Size
	}
	return res
}
