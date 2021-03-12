// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"fmt"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

// Modules is a convenience interface for accessing the various build API
// modules associated with a build.
//
// For information about each build API module, see the corresponding
// `build_api_module` target in //BUILD.gn.
type Modules struct {
	buildDir           string
	apis               []string
	archives           []Archive
	args               Args
	binaries           []Binary
	checkoutArtifacts  []CheckoutArtifact
	generatedSources   []string
	images             []Image
	packageManifests   []string
	platforms          []DimensionSet
	prebuiltBinarySets []PrebuiltBinarySet
	sdkArchives        []SDKArchive
	testSpecs          []TestSpec
	testDurations      []TestDuration
	tools              []Tool
	zbiTests           []ZBITest
}

// NewModules returns a Modules associated with a given build directory.
func NewModules(buildDir string) (*Modules, error) {
	var errMsgs []string
	m := &Modules{buildDir: buildDir}

	if err := jsonutil.ReadFromFile(m.APIManifest(), &m.apis); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.ArchiveManifest(), &m.archives); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.ArgManifest(), &m.args); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.BinaryManifest(), &m.binaries); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.CheckoutArtifactManifest(), &m.checkoutArtifacts); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.GeneratedSourcesManifest(), &m.generatedSources); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.ImageManifest(), &m.images); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.PackageManifestsManifest(), &m.packageManifests); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.PlatformManifest(), &m.platforms); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.PrebuiltBinarySetsManifest(), &m.prebuiltBinarySets); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.SDKArchivesManifest(), &m.sdkArchives); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.TestManifest(), &m.testSpecs); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.TestDurationsManifest(), &m.testDurations); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.ToolManifest(), &m.tools); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.ZBITestManifest(), &m.zbiTests); err != nil {
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

// APIs returns the build API module of available build API modules.
func (m Modules) APIs() []string {
	return m.apis
}

func (m Modules) APIManifest() string {
	return filepath.Join(m.BuildDir(), "api.json")
}

func (m Modules) Archives() []Archive {
	return m.archives
}

func (m Modules) ArchiveManifest() string {
	return filepath.Join(m.BuildDir(), "archives.json")
}

func (m Modules) Args() Args {
	return m.args
}

func (m Modules) ArgManifest() string {
	return filepath.Join(m.BuildDir(), "args.json")
}

func (m Modules) Binaries() []Binary {
	return m.binaries
}

func (m Modules) BinaryManifest() string {
	return filepath.Join(m.BuildDir(), "binaries.json")
}

func (m Modules) CheckoutArtifacts() []CheckoutArtifact {
	return m.checkoutArtifacts
}

func (m Modules) CheckoutArtifactManifest() string {
	return filepath.Join(m.BuildDir(), "checkout_artifacts.json")
}

func (m Modules) GeneratedSources() []string {
	return m.generatedSources
}

func (m Modules) GeneratedSourcesManifest() string {
	return filepath.Join(m.BuildDir(), "generated_sources.json")
}

func (m Modules) Images() []Image {
	return m.images
}

func (m Modules) ImageManifest() string {
	return filepath.Join(m.BuildDir(), "images.json")
}

func (m Modules) PackageManifests() []string {
	return m.packageManifests
}

func (m Modules) PackageManifestsManifest() string {
	return filepath.Join(m.BuildDir(), "all_package_manifest_paths.json")
}

// Platforms returns the build API module of available platforms to test on.
func (m Modules) Platforms() []DimensionSet {
	return m.platforms
}

func (m Modules) PlatformManifest() string {
	return filepath.Join(m.BuildDir(), "platforms.json")
}

// PrebuiltBinarySets returns the build API module of prebuilt packages
// registered in the build.
func (m Modules) PrebuiltBinarySets() []PrebuiltBinarySet {
	return m.prebuiltBinarySets
}

// PrebuiltBinarySetsManifest returns the path to the manifest containing a list
// of prebuilt binary manifests that will be generated by the build.
func (m Modules) PrebuiltBinarySetsManifest() string {
	return filepath.Join(m.BuildDir(), "prebuilt_binaries.json")
}

func (m Modules) SDKArchives() []SDKArchive {
	return m.sdkArchives
}

func (m Modules) SDKArchivesManifest() string {
	return filepath.Join(m.BuildDir(), "sdk_archives.json")
}

func (m Modules) TestDurations() []TestDuration {
	return m.testDurations
}

func (m Modules) TestDurationsManifest() string {
	return filepath.Join(m.BuildDir(), "test_durations.json")
}

func (m Modules) TestSpecs() []TestSpec {
	return m.testSpecs
}

func (m Modules) TestManifest() string {
	return filepath.Join(m.BuildDir(), "tests.json")
}

func (m Modules) Tools() []Tool {
	return m.tools
}

func (m Modules) ToolManifest() string {
	return filepath.Join(m.BuildDir(), "tool_paths.json")
}

func (m Modules) ZBITests() []ZBITest {
	return m.zbiTests
}

func (m Modules) ZBITestManifest() string {
	return filepath.Join(m.BuildDir(), "zbi_tests.json")
}
