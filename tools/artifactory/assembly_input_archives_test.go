// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements assemblyInputArchiveModules.
type mockAssemblyInputArchiveModules struct {
	assemblyInputArchives []build.AssemblyInputArchive
}

func (m mockAssemblyInputArchiveModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockAssemblyInputArchiveModules) AssemblyInputArchives() []build.AssemblyInputArchive {
	return m.assemblyInputArchives
}

func TestAssemblyInputArchiveUploads(t *testing.T) {
	m := &mockAssemblyInputArchiveModules{
		assemblyInputArchives: []build.AssemblyInputArchive{
			{
				Label: "//foo:A",
				Path:  filepath.Join("assembly", "A.tgz"),
			},
			{
				Label: "//foo:B",
				Path:  filepath.Join("assembly", "B.tgz"),
			},
		},
	}
	expected := []Upload{
		{
			Source:      filepath.Join("BUILD_DIR", "assembly", "A.tgz"),
			Destination: filepath.Join("namespace", "A.tgz"),
		},
		{
			Source:      filepath.Join("BUILD_DIR", "assembly", "B.tgz"),
			Destination: filepath.Join("namespace", "B.tgz"),
		},
	}
	actual := assemblyInputArchiveUploads(m, "namespace")
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected assembly input archive uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
