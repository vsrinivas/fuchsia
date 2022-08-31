// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type AssemblyManifest struct {
	// ImageName is the name of the image for which the assembly manifest has been generated.
	ImageName string `json:"image_name"`

	// AssemblyManifestPath is the relative path to the assembly manifest within the build directory.
	AssemblyManifestPath string `json:"assembly_manifest_path"`
}
