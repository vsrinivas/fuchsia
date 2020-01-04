// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package build

import (
	"fmt"
	"path/filepath"
	"strings"
)

const (
	// BinaryModuleName is the name of the build API module of binaries.
	binaryModuleName = "binaries.json"
	// ImageModuleName is the name of the build API module of images.
	imageModuleName = "images.json"
	// PlatformModuleName is the the name of the build API module of valid
	// platforms that tests can target.
	platformModuleName = "platforms.json"
	// TestDurationsName is the name of the file containing expected test
	// durations and timeouts.
	testDurationsName = "test_durations.json"
	// TestModuleName is the name of the build API module of tests.
	testModuleName = "tests.json"
)

// Modules is a convenience interface for accessing the various build API
// modules associated with a build.
type Modules struct {
	buildDir      string
	binaries      []Binary
	images        []Image
	platforms     []DimensionSet
	testSpecs     []TestSpec
	testDurations []TestDuration
}

// NewModules returns a Modules associated with a given build directory.
func NewModules(buildDir string) (*Modules, error) {
	var errMsgs []string
	var err error
	m := &Modules{buildDir: buildDir}

	m.binaries, err = loadBinaries(m.BinaryManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	m.images, err = LoadImages(m.ImageManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	m.platforms, err = loadPlatforms(m.PlatformManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	m.testSpecs, err = loadTestSpecs(m.TestManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	m.testDurations, err = LoadTestDurations(m.TestDurationsManifest())
	if err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if len(errMsgs) > 0 {
		return nil, fmt.Errorf(strings.Join(errMsgs, "\n"))
	}
	return m, nil
}

// BuildDir returns the fuchsia build directory root.
func (m Modules) BuildDir() string {
	return m.buildDir
}

// Binaries returns the build API module of binaries.
func (m Modules) Binaries() []Binary {
	return m.binaries
}

// BinaryManifest returns the path to the manifest of binaries in the build.
func (m Modules) BinaryManifest() string {
	return filepath.Join(m.BuildDir(), binaryModuleName)
}

// Images returns the aggregated build APIs of fuchsia and zircon images.
func (m Modules) Images() []Image {
	return m.images
}

// ImageManifest returns the path to the manifest of images in the build.
func (m Modules) ImageManifest() string {
	return filepath.Join(m.BuildDir(), imageModuleName)
}

// Platforms returns the build API module of available platforms to test on.
func (m Modules) Platforms() []DimensionSet {
	return m.platforms
}

// PlatformManifest returns the path to the manifest of available test platforms.
func (m Modules) PlatformManifest() string {
	return filepath.Join(m.BuildDir(), platformModuleName)
}

// TestDurations returns the build API module of test duration data.
func (m Modules) TestDurations() []TestDuration {
	return m.testDurations
}

// TestDurationsManifest returns the path to the durations file.
func (m Modules) TestDurationsManifest() string {
	return filepath.Join(m.BuildDir(), testDurationsName)
}

// TestSpecs returns the build API module of tests.
func (m Modules) TestSpecs() []TestSpec {
	return m.testSpecs
}

// TestManifest returns the path to the manifest of tests in the build.
func (m Modules) TestManifest() string {
	return filepath.Join(m.BuildDir(), testModuleName)
}
