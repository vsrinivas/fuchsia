// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"path/filepath"

	"golang.org/x/exp/maps"
	"golang.org/x/exp/slices"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

const (
	imageManifestName = "images.json"
)

// Modules is a convenience interface for accessing the various build API
// modules associated with a build.
//
// For information about each build API module, see the corresponding
// `build_api_module` target in //BUILD.gn.
type Modules struct {
	buildDir                 string
	apis                     []string
	archives                 []Archive
	args                     Args
	assemblyInputArchives    []AssemblyInputArchive
	assemblyManifests        []AssemblyManifest
	binaries                 []Binary
	checkoutArtifacts        []CheckoutArtifact
	clippyTargets            []ClippyTarget
	productSizeCheckerOutput []ProductSizeCheckerOutput
	generatedSources         []string
	images                   []Image
	packageManifestsLocation []string
	packageRepositories      []PackageRepo
	platforms                []DimensionSet
	prebuiltBinarySets       []PrebuiltBinarySet
	productBundles           []ProductBundle
	sdkArchives              []SDKArchive
	testListLocation         []string
	testSpecs                []TestSpec
	testDurations            []TestDuration
	tools                    Tools
	zbiTests                 []ZBITest
}

// NewModules returns a Modules associated with a given build directory.
func NewModules(buildDir string) (*Modules, error) {
	m := &Modules{buildDir: buildDir}

	manifests := map[string]interface{}{
		"api.json":                         &m.apis,
		"archives.json":                    &m.archives,
		"args.json":                        &m.args,
		"assembly_input_archives.json":     &m.assemblyInputArchives,
		"assembly_manifests.json":          &m.assemblyManifests,
		"product_size_checker_output.json": &m.productSizeCheckerOutput,
		"binaries.json":                    &m.binaries,
		"checkout_artifacts.json":          &m.checkoutArtifacts,
		"clippy_target_mapping.json":       &m.clippyTargets,
		"generated_sources.json":           &m.generatedSources,
		imageManifestName:                  &m.images,
		"all_package_manifest_paths.json":  &m.packageManifestsLocation,
		"package-repositories.json":        &m.packageRepositories,
		"platforms.json":                   &m.platforms,
		"prebuilt_binaries.json":           &m.prebuiltBinarySets,
		"product_bundles.json":             &m.productBundles,
		"sdk_archives.json":                &m.sdkArchives,
		"tests.json":                       &m.testSpecs,
		"test_durations.json":              &m.testDurations,
		"test_list_location.json":          &m.testListLocation,
		"tool_paths.json":                  &m.tools,
		"zbi_tests.json":                   &m.zbiTests,
	}
	// Ensure we read the manifests in order, so that if multiple manifests are
	// missing we always get the same error.
	manifestNames := maps.Keys(manifests)
	slices.Sort(manifestNames)
	for _, manifest := range manifestNames {
		path := filepath.Join(buildDir, manifest)
		if err := jsonutil.ReadFromFile(path, manifests[manifest]); err != nil {
			return nil, err
		}
	}
	return m, nil
}

// BuildDir returns the fuchsia build directory root.
func (m Modules) BuildDir() string {
	return m.buildDir
}

// ImageManifest returns the path to the images manifest.
func (m Modules) ImageManifest() string {
	return filepath.Join(m.BuildDir(), imageManifestName)
}

// APIs returns the build API module of available build API modules.
func (m Modules) APIs() []string {
	return m.apis
}

func (m Modules) Archives() []Archive {
	return m.archives
}

func (m Modules) Args() Args {
	return m.args
}

func (m Modules) AssemblyInputArchives() []AssemblyInputArchive {
	return m.assemblyInputArchives
}

func (m Modules) AssemblyManifests() []AssemblyManifest {
	return m.assemblyManifests
}

func (m Modules) Binaries() []Binary {
	return m.binaries
}

func (m Modules) CheckoutArtifacts() []CheckoutArtifact {
	return m.checkoutArtifacts
}

func (m Modules) ClippyTargets() []ClippyTarget {
	return m.clippyTargets
}

func (m Modules) GeneratedSources() []string {
	return m.generatedSources
}

func (m Modules) Images() []Image {
	return m.images
}

func (m Modules) PackageManifestsLocation() []string {
	return m.packageManifestsLocation
}

func (m Modules) PackageRepositories() []PackageRepo {
	return m.packageRepositories
}

// Platforms returns the build API module of available platforms to test on.
func (m Modules) Platforms() []DimensionSet {
	return m.platforms
}

// PrebuiltBinarySets returns the build API module of prebuilt packages
// registered in the build.
func (m Modules) PrebuiltBinarySets() []PrebuiltBinarySet {
	return m.prebuiltBinarySets
}

func (m Modules) ProductSizeCheckerOutput() []ProductSizeCheckerOutput {
	return m.productSizeCheckerOutput
}

func (m Modules) ProductBundles() []ProductBundle {
	return m.productBundles
}

func (m Modules) SDKArchives() []SDKArchive {
	return m.sdkArchives
}

func (m Modules) TestDurations() []TestDuration {
	return m.testDurations
}

func (m Modules) TestListLocation() []string {
	return m.testListLocation
}

func (m Modules) TestSpecs() []TestSpec {
	return m.testSpecs
}

func (m Modules) Tools() Tools {
	return m.tools
}

func (m Modules) ZBITests() []ZBITest {
	return m.zbiTests
}
