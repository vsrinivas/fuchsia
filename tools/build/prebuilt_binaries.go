// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package build

import (
	"errors"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

// PrebuiltBinarySet represents a manifest referencing a set of prebuilt
// binaries.
type PrebuiltBinarySet struct {
	// Name is the name of the set of prebuilt binaries.
	Name string `json:"name"`

	// Manifest is the path to a manifest of the associated debug binaries produced
	// by the build and in the format of binaries.json.
	Manifest string `json:"manifest"`
}

// Get returns the list of binaries in enumerated in the associated
// binary manifest.
// Returns os.ErrIsNotExist if the file does not exist.
func (pb *PrebuiltBinarySet) Get(buildDir string) ([]Binary, error) {
	if pb.Manifest == "" {
		return nil, errors.New("no associated debug binary manifest")
	}
	exists, err := osmisc.FileExists(filepath.Join(buildDir, pb.Manifest))
	if err != nil {
		return nil, err
	} else if !exists {
		return nil, os.ErrNotExist
	}
	var binaries []Binary
	return binaries, jsonutil.ReadFromFile(filepath.Join(buildDir, pb.Manifest), &binaries)
}
