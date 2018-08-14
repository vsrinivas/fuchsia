// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// Snapshot contains metadata from one or more packages
type Snapshot struct {
	Packages map[string]Package      `json:"packages"`
	Blobs    map[MerkleRoot]BlobInfo `json:"blobs"`
}

// BlobInfo contains metadata about a single blob
type BlobInfo struct {
	Size int `json:"size"`
}

// Package contains metadata about a package, including a list of all files in
// the package
type Package struct {
	Files map[string]MerkleRoot `json:"files"`
	Tags  []string              `json:"tags,omitempty"`
}
