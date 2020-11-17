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

// Implements sdkArchiveModules
type mockSDKArchiveModules struct {
	sdkArchives []build.SDKArchive
}

func (m mockSDKArchiveModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockSDKArchiveModules) SDKArchives() []build.SDKArchive {
	return m.sdkArchives
}

func TestSDKArchiveUploads(t *testing.T) {
	m := &mockSDKArchiveModules{
		sdkArchives: []build.SDKArchive{
			{
				Name:  "A",
				Label: "//foo:A",
				Path:  filepath.Join("sdk", "archive", "A.tar.gz"),
				OS:    "fuchsia",
				CPU:   "x64",
			},
			{
				Name:  "B",
				Label: "//foo:B",
				Path:  filepath.Join("host_x64", "sdk", "archive", "B.tar.gz"),
				OS:    "linux",
				CPU:   "x64",
			},
			{
				Name:  "B",
				Label: "//foo:B",
				Path:  filepath.Join("host_arm64", "sdk", "archive", "B.tar.gz"),
				OS:    "linux",
				CPU:   "arm64",
			},
			{
				Name:  "B",
				Label: "//foo:B",
				Path:  filepath.Join("host_x64", "sdk", "archive", "B.tar.gz"),
				OS:    "mac",
				CPU:   "x64",
			},
		},
	}
	expected := []Upload{
		{
			Source:      filepath.Join("BUILD_DIR", "sdk", "archive", "A.tar.gz"),
			Destination: filepath.Join("namespace", "A.tar.gz"),
		},
		{
			Source:      filepath.Join("BUILD_DIR", "host_x64", "sdk", "archive", "B.tar.gz"),
			Destination: filepath.Join("namespace", "linux-x64", "B.tar.gz"),
		},
		{
			Source:      filepath.Join("BUILD_DIR", "host_arm64", "sdk", "archive", "B.tar.gz"),
			Destination: filepath.Join("namespace", "linux-arm64", "B.tar.gz"),
		},
		{
			Source:      filepath.Join("BUILD_DIR", "host_x64", "sdk", "archive", "B.tar.gz"),
			Destination: filepath.Join("namespace", "mac-x64", "B.tar.gz"),
		},
	}
	actual := sdkArchiveUploads(m, "namespace")
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected SDK archive uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
