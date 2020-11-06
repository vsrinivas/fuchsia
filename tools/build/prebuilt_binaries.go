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

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

// PrebuiltBinaries represents a set of prebuilt binaries.
type PrebuiltBinaries struct {
	// Name is the name of the set of prebuilt binaries.
	Name string `json:"package_name"`

	// DebugArchive is an optional pointer to an archive of debug binaries
	// relating to the prebuilt package.
	DebugArchive string `json:"debug_archive,omitempty"`

	// Manifest is the path to a manifest of the associated debug binaries produced
	// by the build and in the format of binaries.json.
	Manifest string `json:"manifest"`
}

// Get returns the list of binaries in enumerated in the associated
// binary manifest.
// Returns os.ErrIsNotExist if the file does not exist.
func (pb *PrebuiltBinaries) Get(buildDir string) ([]Binary, error) {
	if pb.Manifest == "" {
		return nil, errors.New("no associated debug binary manifest")
	}
	exists, err := osmisc.FileExists(filepath.Join(buildDir, pb.Manifest))
	if err != nil {
		return nil, err
	} else if !exists {
		return nil, os.ErrNotExist
	}
	return loadBinaries(filepath.Join(buildDir, pb.Manifest))
}

// loadPrebuiltBinaries reads in the entries indexed in the given prebuilt package manifest.
func loadPrebuiltBinaries(manifest string) ([]PrebuiltBinaries, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %w", manifest, err)
	}
	defer f.Close()
	var pkgs []PrebuiltBinaries
	if err := json.NewDecoder(f).Decode(&pkgs); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %w", manifest, err)
	}
	return pkgs, nil
}
