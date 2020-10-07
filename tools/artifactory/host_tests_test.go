// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
)

func TestHostTestUploads(t *testing.T) {
	buildDir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("ioutil.TempDir() failed: %v", err)
	}
	defer func() {
		if err := os.RemoveAll(buildDir); err != nil {
			t.Error(err)
		}
	}()
	runtimeDepsRelPath := "runtime_deps.json"
	testSpecs := []build.TestSpec{
		{Test: build.Test{OS: "linux", Path: "foo"}},
		{Test: build.Test{OS: "something else"}},
		{Test: build.Test{OS: "mac", Path: "bar", RuntimeDepsFile: runtimeDepsRelPath}},
	}
	runtimeDeps := []string{"a", filepath.Join("b", "c.txt"), "a", testSpecs[0].Test.Path}
	runtimeDepsBytes, err := json.Marshal(runtimeDeps)
	if err != nil {
		t.Fatalf("failed to Marhsal(runtimeDeps): %v", err)
	}
	if err := ioutil.WriteFile(filepath.Join(buildDir, runtimeDepsRelPath), runtimeDepsBytes, 0444); err != nil {
		t.Fatalf("failed to write runtimeDepsRelPath: %v", err)
	}
	// The regular files inside runtime deps, some of which are directories.
	runtimeDepFilePaths := []string{filepath.Join(runtimeDeps[0], "a.txt"), runtimeDeps[1], runtimeDeps[3]}
	for _, filePath := range runtimeDepFilePaths {
		absPath := filepath.Join(buildDir, filePath)
		parentDir := filepath.Dir(absPath)
		if err = os.MkdirAll(parentDir, 0700); err != nil {
			t.Fatalf("os.MkdirAll(%s) failed: %v", parentDir, err)
		}
		if err = ioutil.WriteFile(absPath, []byte("\n"), 0700); err != nil {
			t.Fatalf("ioutil.WriteFile(%s) failed: %v", absPath, err)
		}
	}

	uploads, err := HostTestUploads(testSpecs, buildDir, "namespace")
	if err != nil {
		t.Errorf("HostTestUploads() failed: %v", err)
	}
	wantUploads := []Upload{
		{
			Source:      filepath.Join(buildDir, testSpecs[0].Test.Path),
			Destination: path.Join("namespace", testSpecs[0].Test.Path),
		},
		// testSpecs[1] is not a host test
		{
			Source:      filepath.Join(buildDir, testSpecs[2].Test.Path),
			Destination: path.Join("namespace", testSpecs[2].Test.Path),
		},
	}
	// runtimeDepFilePaths[2] has the same path as testSpecs[0], so we don't expect to
	// see it twice.
	for _, filePath := range runtimeDepFilePaths[:2] {
		wantUploads = append(wantUploads, Upload{
			Source:      filepath.Join(buildDir, filePath),
			Destination: path.Join("namespace", filePath),
		})
	}
	if diff := cmp.Diff(wantUploads, uploads); diff != "" {
		t.Errorf("HostTestUploads()=%v\nwant=%v\ndiff (-want +got):\n%s", uploads, wantUploads, diff)
	}
}
