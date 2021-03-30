// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testdriver

import (
	"testing"
)

func TestNewDriver(t *testing.T) {
	d := NewDriver()

	path := "/test/path"
	d.SetWorkspacePath(path)
	if d.workspacePath != path {
		t.Errorf("workspacePath: want: %s, got: %s\n", path, d.workspacePath)
	}

	version := "123456789"
	d.SetSDKVersion(version)
	if d.sdkVersion != version {
		t.Errorf("sdkVersion: want: %s, got: %s\n", version, d.sdkVersion)
	}

	manifest := "/test/path/manifest.json"
	d.SetManifestPath(manifest)
	if d.manifestPath != manifest {
		t.Errorf("manifestPath: want: %s, got: %s\n", manifest, d.manifestPath)
	}

}
