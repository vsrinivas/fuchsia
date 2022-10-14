// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// ProductBundle represents an entry in the product bundles build api.
type ProductBundle struct {
	// Path is the path to the product bundle directory.
	Path string `json:"path"`

	// Label is the GN label for the product bundle.
	Label string `json:"label"`

	// TransferManifestPath is the path to the transfer manifest for the product
	// bundle.
	TransferManifestPath string `json:"transfer_manifest_path"`
}
