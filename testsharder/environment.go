// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The GN environments specified by test authors in the Fuchsia source
// correspond directly to the Environment struct defined here.
//
// Note that by "platforms" we mean a specific group of dimension sets which
// correspond to the currently available test platforms supported by the
// infrastructure.
package testsharder

import (
	"encoding/json"
	"io/ioutil"
	"path/filepath"

	"fuchsia.googlesource.com/tools/build"
)

// Environment describes the full environment a test requires.
type Environment struct {
	// Dimensions gives the Swarming dimensions a test wishes to target.
	Dimensions DimensionSet `json:"dimensions"`

	// Label is a label given to an environment on which the testsharder may filter.
	Label string `json:"label,omitempty"`
}

// Name returns a name calculated from its specfied properties.
func (env Environment) Name() string {
	// For now, this just returns the device type or OS; later it will be more clever.
	if env.Dimensions.DeviceType != "" {
		return env.Dimensions.DeviceType
	} else {
		return env.Dimensions.OS
	}
}

// DimensionSet encapsulate the Swarming dimensions a test wishes to target.
type DimensionSet struct {
	// DeviceType represents the class of device the test should run on.
	// This is a required field.
	DeviceType string `json:"device_type,omitempty"`

	// The OS to run the test on (e.g., "Linux" or "Mac"). Used for host-side testing.
	OS string `json:"os,omitempty"`
}

// ResolvesTo gives a partial ordering on DimensionSets in which one resolves to
// another if the former's dimensions are given the latter.
func (dims DimensionSet) resolvesTo(other DimensionSet) bool {
	if dims.DeviceType != "" && dims.DeviceType != other.DeviceType {
		return false
	}
	if dims.OS != "" && dims.OS != other.OS {
		return false
	}
	return true
}

// LoadPlatforms loads the list of test platforms specified as a JSON list
// produced by a build, given the root of the build directory.
func LoadPlatforms(fuchsiaBuildDir string) ([]DimensionSet, error) {
	platformManifestPath := filepath.Join(fuchsiaBuildDir, build.PlatformManifestName)
	bytes, err := ioutil.ReadFile(platformManifestPath)
	if err != nil {
		return nil, err
	}
	var platforms []DimensionSet
	if err = json.Unmarshal(bytes, &platforms); err != nil {
		return nil, err
	}
	return platforms, err
}
