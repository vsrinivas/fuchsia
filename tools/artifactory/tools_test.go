// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements toolModules
type mockToolModules struct {
	tools []build.Tool
}

func (m mockToolModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockToolModules) Tools() []build.Tool {
	return m.tools
}

func TestToolUploads(t *testing.T) {
	m := &mockToolModules{
		tools: []build.Tool{
			{
				Name: "A",
				Path: filepath.Join("host_arm64", "A"),
				OS:   "linux",
				CPU:  "arm64",
			},
			{
				Name: "A",
				Path: filepath.Join("host_x64", "A"),
				OS:   "linux",
				CPU:  "x64",
			},
			{
				Name: "B",
				Path: filepath.Join("host_arm64", "B"),
				OS:   "linux",
				CPU:  "arm64",
			},
			{
				Name: "B",
				Path: filepath.Join("host_x64", "B"),
				OS:   "linux",
				CPU:  "x64",
			},
			{
				Name: "C",
				Path: filepath.Join("host_x64", "C"),
				OS:   "linux",
				CPU:  "x64",
			},
		},
	}
	expected := []Upload{
		{
			Source:      filepath.Join("BUILD_DIR", "host_arm64", "A"),
			Destination: "namespace/linux-arm64/A",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "host_x64", "A"),
			Destination: "namespace/linux-x64/A",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "host_x64", "C"),
			Destination: "namespace/linux-x64/C_dest",
		},
	}

	allowlist := map[string]string{
		"A": "A",
		"C": "C_dest",
	}
	actual := toolUploads(m, allowlist, "namespace")
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected tool uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
