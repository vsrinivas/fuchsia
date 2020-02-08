// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Implements binModules
type mockBinModules struct {
	bins []build.Binary
}

func (m mockBinModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockBinModules) Binaries() []build.Binary {
	return m.bins
}

func TestDebugBinaryUploads(t *testing.T) {

	// For the ease of testing, we pretend that the first three of the binaries
	// below lie in prebuilt build-id directories (so that ELFBuildID() returns
	// straightforwardly).
	prebuiltBuildIDDir := filepath.Join("..", "..", "prebuilt", ".build-id")
	m := &mockBinModules{
		bins: []build.Binary{
			{
				Debug:    filepath.Join(prebuiltBuildIDDir, "fi", "rst.debug"),
				Breakpad: filepath.Join("gen", "first.sym"),
				OS:       "fuchsia",
			},
			{
				Debug:    filepath.Join(prebuiltBuildIDDir, "se", "cond.debug"),
				Breakpad: filepath.Join("host", "gen", "second.sym"),
				OS:       "linux",
			},
			{
				Debug: filepath.Join(prebuiltBuildIDDir, "th", "ird.debug"),
				OS:    "fuchsia",
			},
			{
				Debug: "binD",
				OS:    "fuchsia",
			},
		},
	}

	expectedUploads := []Upload{
		{
			Source:      filepath.Join("BUILD_DIR", prebuiltBuildIDDir, "fi", "rst.debug"),
			Destination: "NAMESPACE/first.debug",
			Deduplicate: true,
		},
		{
			Source:      filepath.Join("BUILD_DIR", "gen", "first.sym"),
			Destination: "NAMESPACE/first.sym",
			Deduplicate: true,
		},
		{
			Source:      filepath.Join("BUILD_DIR", prebuiltBuildIDDir, "se", "cond.debug"),
			Destination: "NAMESPACE/second.debug",
			Deduplicate: true,
		},
		{
			Source:      filepath.Join("BUILD_DIR", "host", "gen", "second.sym"),
			Destination: "NAMESPACE/second.sym",
			Deduplicate: true,
		},
		{
			Source:      filepath.Join("BUILD_DIR", prebuiltBuildIDDir, "th", "ird.debug"),
			Destination: "NAMESPACE/third.debug",
			Deduplicate: true,
		},
	}
	expectedIDs := []string{"first", "third"}

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
