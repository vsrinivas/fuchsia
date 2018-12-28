// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes build-specific concepts.

package build

import (
	"encoding/json"
	"os"
	"path/filepath"
)

const (
	// PackageManifestName is the name of the manifest of fuchsia package
	// targets included in a build.
	packageManifestName = "packages.json"

	// HostTestManifestName is the name of the manifest of host-side test
	// targets included in a build.
	hostTestManifestName = "host_tests.json"

	// PlatformManifestName is the name of the manifest of available test
	// platforms.
	PlatformManifestName = "platforms.json"
)

// Target provides information about a GN target.
type Target struct {
	// BuildDir is a relative path in the Fuchsia build directory to the location
	// of the target's generated file directory.
	BuildDir string `json:"build_dir"`

	// Dir is the source-relative directory of the target (e.g.,
	// //garnet/dir/of/build-dot-gn-file).
	Dir string `json:"dir"`

	// Name is the name of the target.
	Name string `json:"name"`
}

type pkgManifest struct {
	Pkgs []Target `json:"packages"`
}

// LoadPackages loads a list of packages targets from JSON package manifest
// produced by the build, given the root of the build directory.
func LoadPackages(fuchsiaBuildDir string) ([]Target, error) {
	manifestPath := filepath.Join(fuchsiaBuildDir, packageManifestName)
	manifestFile, err := os.Open(manifestPath)
	if err != nil {
		return nil, err
	}
	defer manifestFile.Close()
	var pkgManifest pkgManifest
	err = json.NewDecoder(manifestFile).Decode(&pkgManifest)
	return pkgManifest.Pkgs, err
}

// LoadHostTests loads a list of host test targets from a JSON host test
// manifest produced by the build, given the root of the build directory.
func LoadHostTests(fuchsiaBuildDir string) ([]Target, error) {
	manifestPath := filepath.Join(fuchsiaBuildDir, hostTestManifestName)
	manifestFile, err := os.Open(manifestPath)
	// TODO(IN-823): As of writing this, the host test manifest does not exist. Until it
	// exists make non-existence not a point of failure.
	if os.IsNotExist(err) {
		return []Target{}, nil
	} else if err != nil {
		return nil, err
	}
	defer manifestFile.Close()
	var hostTests []Target
	err = json.NewDecoder(manifestFile).Decode(&hostTests)
	return hostTests, err
}
