// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

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
