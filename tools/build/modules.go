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

const (
	apiModuleName              = "api.json"
	archiveModuleName          = "archives.json"
	argsModuleName             = "args.json"
	binaryModuleName           = "binaries.json"
	checkoutArtifactModuleName = "checkout_artifacts.json"
	imageModuleName            = "images.json"
	packageManifestModuleName  = "all_package_manifest_paths.json"
	platformModuleName         = "platforms.json"
	prebuiltBinaryModuleName   = "prebuilt_binaries.json"
	sdkArchivesModuleName      = "sdk_archives.json"
	testDurationsName          = "test_durations.json"
	testModuleName             = "tests.json"
	toolModuleName             = "tool_paths.json"
)

// Modules is a convenience interface for accessing the various build API
// modules associated with a build.
type Modules struct {
	buildDir          string
	apis              []string
	archives          []Archive
	args              Args
	binaries          []Binary
	checkoutArtifacts []CheckoutArtifact
	images            []Image
	packageManifests  []string
	platforms         []DimensionSet
	prebuiltBins      []PrebuiltBinaries
	sdkArchives       []SDKArchive
	testSpecs         []TestSpec
	testDurations     []TestDuration
	tools             []Tool
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

	if err := jsonutil.ReadFromFile(m.ImageManifest(), &m.images); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.PackageManifestsManifest(), &m.packageManifests); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.PlatformManifest(), &m.platforms); err != nil {
		errMsgs = append(errMsgs, err.Error())
	}

	if err := jsonutil.ReadFromFile(m.PrebuiltBinaryManifest(), &m.prebuiltBins); err != nil {
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

// APIManifest returns the path to the manifest of build API modules present in the build.
func (m Modules) APIManifest() string {
	return filepath.Join(m.BuildDir(), apiModuleName)
}

func (m Modules) Archives() []Archive {
	return m.archives
}

func (m Modules) ArchiveManifest() string {
	return filepath.Join(m.BuildDir(), archiveModuleName)
}

// Args returns the build API module of args set in the build.
func (m Modules) Args() Args {
	return m.args
}

// ArgManifest returns the path to the manifest of GN args set in the build.
func (m Modules) ArgManifest() string {
	return filepath.Join(m.BuildDir(), argsModuleName)
}

// Binaries returns the build API module of binaries.
func (m Modules) Binaries() []Binary {
	return m.binaries
}

// BinaryManifest returns the path to the manifest of binaries in the build.
func (m Modules) BinaryManifest() string {
	return filepath.Join(m.BuildDir(), binaryModuleName)
}

// CheckoutArtifacts returns the build API module of checkout artifacts.
func (m Modules) CheckoutArtifacts() []CheckoutArtifact {
	return m.checkoutArtifacts
}

// CheckoutArtifactManifest returns the path to the manifest of checkout artifacts in the build.
func (m Modules) CheckoutArtifactManifest() string {
	return filepath.Join(m.BuildDir(), checkoutArtifactModuleName)
}

// Images returns the aggregated build APIs of fuchsia and zircon images.
func (m Modules) Images() []Image {
	return m.images
}

// ImageManifest returns the path to the manifest of images in the build.
func (m Modules) ImageManifest() string {
	return filepath.Join(m.BuildDir(), imageModuleName)
}

// PackageManifests returns a list of paths to all the universe package manifests.
func (m Modules) PackageManifests() []string {
	return m.packageManifests
}

// PackageManifestsManifest returns the path to the manifest of universe package manifests in the build.
func (m Modules) PackageManifestsManifest() string {
	return filepath.Join(m.BuildDir(), packageManifestModuleName)
}

// Platforms returns the build API module of available platforms to test on.
func (m Modules) Platforms() []DimensionSet {
	return m.platforms
}

// PlatformManifest returns the path to the manifest of available test platforms.
func (m Modules) PlatformManifest() string {
	return filepath.Join(m.BuildDir(), platformModuleName)
}

// PrebuiltBinaries returns the build API module of prebuilt packages registered in the build.
func (m Modules) PrebuiltBinaries() []PrebuiltBinaries {
	return m.prebuiltBins
}

// PrebuiltBinaryManifest returns the path to the manifest of prebuilt packages.
func (m Modules) PrebuiltBinaryManifest() string {
	return filepath.Join(m.BuildDir(), prebuiltBinaryModuleName)
}

// SDKArchives returns the build API module of SDK archives.
func (m Modules) SDKArchives() []SDKArchive {
	return m.sdkArchives
}

// SDKArchivesManifest returns the path to the manifest of SDK archives.
func (m Modules) SDKArchivesManifest() string {
	return filepath.Join(m.BuildDir(), sdkArchivesModuleName)
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

// Tools returns the build API module of tools.
func (m Modules) Tools() []Tool {
	return m.tools
}

// ToolManifest returns the path to the manifest of tools in the build.
func (m Modules) ToolManifest() string {
	return filepath.Join(m.BuildDir(), toolModuleName)
}
