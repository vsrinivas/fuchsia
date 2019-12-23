// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
)

// PackageBlobInfo contains metadata for a single blob in a package
type PackageBlobInfo struct {
	// The path of the blob relative to the output directory
	SourcePath string `json:"source_path"`

	// The path within the package
	Path string `json:"path"`

	// Merkle root for the blob
	Merkle MerkleRoot `json:"merkle"`

	// Size of blob, in bytes
	Size uint64 `json:"size"`
}

// LoadBlobs attempts to read and parse a blobs manifest from the given path
func LoadBlobs(path string) ([]PackageBlobInfo, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var members []PackageBlobInfo

	err = json.Unmarshal(data, &members)
	if err != nil {
		return nil, err
	}

	packagePaths := make(map[string]struct{})
	for _, blob := range members {
		if _, ok := packagePaths[blob.Path]; ok {
			return nil, fmt.Errorf("%q contains more than one entry with path = %q", path, blob.Path)
		}
		packagePaths[blob.Path] = struct{}{}
	}

	return members, nil
}
