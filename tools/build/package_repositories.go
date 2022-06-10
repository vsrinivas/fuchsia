// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type PackageRepo struct {
	// Blobs is the path to the blobs directory.
	Blobs string `json:"blobs"`

	// Path is the path to the repository.
	Path string `json:"path"`

	// Targets is the path to targets.json within the repository.
	Targets string `json:"targets"`
}
