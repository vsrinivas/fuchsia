// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package build

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
)

// PrebuiltPackage represents a prebuilt package in the checkout.
type PrebuiltPackage struct {
	// Name is the name of the package.
	Name string `json:"package_name`

	// Archive points to a fuchsia archive (a FAR) of the package.
	Archive string `json:"archive"`

	// DebugArchive is an optional pointer to an archive of debug binaries
	// relating to the prebuilt package.
	DebugArchive string `json:"debug_archive,omitempty"`

	// BinaryManifest is an optional path to a manifest of the associated
	// debug binaries produced by the build and in the format of binaries.json.
	BinaryManifest string `json:"binaries,omitempty"`
}

// Binaries returns the list of binaries in enumerated in the associated
// binary manifest.
func (pp *PrebuiltPackage) Binaries(buildDir string) ([]Binary, error) {
	if pp.BinaryManifest == "" {
		return nil, errors.New("no associated debug binary manifest")
	}
	return loadBinaries(filepath.Join(buildDir, pp.BinaryManifest))
}

// loadPrebuiltPackages reads in the entries indexed in the given prebuilt package manifest.
func loadPrebuiltPackages(manifest string) ([]PrebuiltPackage, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %w", manifest, err)
	}
	defer f.Close()
	var pkgs []PrebuiltPackage
	if err := json.NewDecoder(f).Decode(&pkgs); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %w", manifest, err)
	}
	return pkgs, nil
}
