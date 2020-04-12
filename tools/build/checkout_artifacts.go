// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"os"
)

// CheckoutArtifact represents an entry in a checkout artifact manifest.
type CheckoutArtifact struct {
	// Name is the canonical name of the artifact.
	Name string `json:"name"`

	// Path is the artifact, relative to the build directory.
	Path string `json:"path"`

	// Type is the shorthand for the type of the artifact.
	Type string `json:"type"`
}

func loadCheckoutArtifacts(manifest string) ([]CheckoutArtifact, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %w", manifest, err)
	}
	defer f.Close()
	var artifacts []CheckoutArtifact
	if err := json.NewDecoder(f).Decode(&artifacts); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %w", manifest, err)
	}
	return artifacts, nil
}
