// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type AssemblyInputArchive struct {
	// Label is the GN label of the associated `assembly_input_bundle_archive`
	// target.
	Label string `json:"label"`

	// Path is the relative path to the archive within the build directory.
	Path string `json:"path"`
}
