// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// CheckoutArtifact represents an entry in a checkout artifact manifest.
type CheckoutArtifact struct {
	// Name is the canonical name of the artifact.
	Name string `json:"name"`

	// Path is the artifact, relative to the build directory.
	Path string `json:"path"`

	// Type is the shorthand for the type of the artifact.
	Type string `json:"type"`
}
