// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Implements binModules
type mockBinModules struct {
	buildDir string
	bins     []build.Binary
	pkgs     []build.PrebuiltPackage
}

func (m mockBinModules) BuildDir() string {
	return m.buildDir
}

func (m mockBinModules) Binaries() []build.Binary {
	return m.bins
}

func (m mockBinModules) PrebuiltPackages() []build.PrebuiltPackage {
	return m.pkgs
}

func TestDebugBinaryUploads(t *testing.T) {
	buildDir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create a temporary directory: %v", err)
	}
	defer os.RemoveAll(buildDir)
	println(buildDir)

	prebuiltBinManifest, err := binaryManifest(buildDir, []build.Binary{
		{
			Debug:    filepath.Join(".build-id", "pr", "ebuiltA.debug"),
			Breakpad: filepath.Join("gen", "prebuiltA.sym"),
			OS:       "fuchsia",
		},
		{
			Debug:    filepath.Join(".build-id", "pr", "ebuiltA.debug"),
			Breakpad: filepath.Join("gen", "prebuiltA.sym"),
			OS:       "fuchsia",
		},
		{
			Debug:    filepath.Join(".build-id", "pr", "ebuiltB.debug"),
			Breakpad: filepath.Join("host", "gen", "prebuiltB.sym"),
			OS:       "linux",
		},
	})
	if err != nil {
		t.Fatalf("failed to create binary manifest: %v", err)
	}

	m := &mockBinModules{
		buildDir: buildDir,
		bins: []build.Binary{
			{
				Debug:    filepath.Join(".build-id", "fi", "rst.debug"),
				Breakpad: filepath.Join("gen", "first.sym"),
				OS:       "fuchsia",
			},
			{
				Debug:    filepath.Join(".build-id", "fi", "rst.debug"),
				Breakpad: filepath.Join("gen", "first.sym"),
				OS:       "fuchsia",
			},
			{
				Debug:    filepath.Join(".build-id", "se", "cond.debug"),
				Breakpad: filepath.Join("host", "gen", "second.sym"),
				OS:       "linux",
			},
			{
				Debug: filepath.Join(".build-id", "th", "ird.debug"),
				OS:    "fuchsia",
			},
			{
				Debug: "binD",
				OS:    "fuchsia",
			},
		},
		pkgs: []build.PrebuiltPackage{
			{
				Name:           "prebuilt",
				BinaryManifest: prebuiltBinManifest,
			},
		},
	}

	expectedUploads := []Upload{
		{
			Source:      filepath.Join(buildDir, ".build-id", "fi", "rst.debug"),
			Destination: "NAMESPACE/first.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, "gen", "first.sym"),
			Destination: "NAMESPACE/first.sym",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "se", "cond.debug"),
			Destination: "NAMESPACE/second.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, "host", "gen", "second.sym"),
			Destination: "NAMESPACE/second.sym",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "th", "ird.debug"),
			Destination: "NAMESPACE/third.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "pr", "ebuiltA.debug"),
			Destination: "NAMESPACE/prebuiltA.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, "gen", "prebuiltA.sym"),
			Destination: "NAMESPACE/prebuiltA.sym",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "pr", "ebuiltB.debug"),
			Destination: "NAMESPACE/prebuiltB.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, "host", "gen", "prebuiltB.sym"),
			Destination: "NAMESPACE/prebuiltB.sym",
			Compress:    true,
			Deduplicate: true,
		},
	}
	expectedIDs := []string{"first", "third", "prebuiltA"}

	actualUploads, actualIDs, err := debugBinaryUploads(m, "NAMESPACE")
	if err != nil {
		t.Fatalf("failed to generate debug binary uploads: %v", err)
	}
	if !reflect.DeepEqual(actualUploads, expectedUploads) {
		t.Fatalf("unexpected debug binary uploads:\nexpected:\n%#v\nactual:\n%#v\n", expectedUploads, actualUploads)
	}
	if !reflect.DeepEqual(actualIDs, expectedIDs) {
		t.Fatalf("unexpected build IDs:\nexpected:\n%#v\nactual:\n%#v\n", expectedIDs, actualIDs)
	}
}

func binaryManifest(buildDir string, bins []build.Binary) (string, error) {
	manifest, err := ioutil.TempFile(buildDir, "artifactory_tests")
	if err != nil {
		return "", err
	}
	defer manifest.Close()
	if err := json.NewEncoder(manifest).Encode(&bins); err != nil {
		return "", fmt.Errorf("failed to encode binary manifest: %v", err)
	}
	return filepath.Base(manifest.Name()), nil
}
