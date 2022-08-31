// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements assemblyManifestModules.
type mockAssemblyManifestsModules struct {
	assemblyManifests []build.AssemblyManifest
}

func (m mockAssemblyManifestsModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockAssemblyManifestsModules) AssemblyManifests() []build.AssemblyManifest {
	return m.assemblyManifests
}

func TestAssemblyManifestsUploads(t *testing.T) {
	m := &mockAssemblyManifestsModules{
		assemblyManifests: []build.AssemblyManifest{
			{
				ImageName:            "fuchsia",
				AssemblyManifestPath: "obj/manifest.xyz",
			},
			{
				ImageName:            "zedboot",
				AssemblyManifestPath: "FOO/assemble.json",
			},
		},
	}
	expected := []Upload{
		{
			Source:      "BUILD_DIR/obj/manifest.xyz",
			Destination: "namespace/fuchsia.json",
		},
		{
			Source:      "BUILD_DIR/FOO/assemble.json",
			Destination: "namespace/zedboot.json",
		},
	}
	actual := assemblyManifestsUploads(m, "namespace")
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected assembly manifests uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
