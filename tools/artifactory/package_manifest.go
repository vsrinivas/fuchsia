// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"fmt"
	"io/ioutil"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
)

// loadPackageManifest parses the package manifest for a particular package.
func loadPackageManifest(packageManifestPath string) (*build.PackageManifest, error) {
	fileContents, err := ioutil.ReadFile(packageManifestPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read %s: %w", packageManifestPath, err)
	}

	manifest := &build.PackageManifest{}
	if err := json.Unmarshal(fileContents, manifest); err != nil {
		return nil, fmt.Errorf("failed to unmarshal %s: %w", packageManifestPath, err)
	}

	if manifest.Version != "1" {
		return nil, fmt.Errorf("unknown version %q, can't load manifest", manifest.Version)
	}

	return manifest, nil
}
