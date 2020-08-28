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

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements binModules
type mockBinModules struct {
	buildDir string
	bins     []build.Binary
	pbins    []build.PrebuiltBinaries
}

func (m mockBinModules) BuildDir() string {
	return m.buildDir
}

func (m mockBinModules) Args() build.Args {
	val := true
	trueMsg, err := json.Marshal(&val)
	if err != nil {
		panic("was not able to marshal `true`: " + err.Error())
	}
	return build.Args(map[string]json.RawMessage{
		"output_breakpad_syms": trueMsg,
	})
}

func (m mockBinModules) Binaries() []build.Binary {
	return m.bins
}

func (m mockBinModules) PrebuiltBinaries() []build.PrebuiltBinaries {
	return m.pbins
}

func TestDebugBinaryUploads(t *testing.T) {
	checkout, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create a temporary directory: %v", err)
	}
	defer os.RemoveAll(checkout)
	buildDir, err := ioutil.TempDir(checkout, "out")
	if err != nil {
		t.Fatalf("failed to create a 'build directory'")
	}

	pbins := []build.Binary{
		{
			Debug:    filepath.Join("..", ".build-id", "pr", "ebuiltA.debug"),
			Breakpad: filepath.Join("..", ".build-id", "pr", "ebuiltA.sym"),
			OS:       "fuchsia",
			Label:    "//prebuilt",
		},
		{
			Debug:    filepath.Join("..", ".build-id", "pr", "ebuiltA.debug"),
			Breakpad: filepath.Join("..", ".build-id", "pr", "ebuiltA.sym"),
			OS:       "fuchsia",
			Label:    "//prebuilt",
		},
		{
			Debug:    filepath.Join("..", ".build-id", "pr", "ebuiltB.debug"),
			Breakpad: filepath.Join("..", ".build-id", "pr", "ebuiltB.sym"),
			OS:       "linux",
			Label:    "//prebuilt",
		},
	}
	prebuiltBinManifest, err := binaryManifest(buildDir, pbins)
	if err != nil {
		t.Fatalf("failed to create binary manifest: %v", err)
	}

	m := &mockBinModules{
		buildDir: buildDir,
		bins: []build.Binary{
			{
				Debug:    filepath.Join(".build-id", "fi", "rst.debug"),
				Dist:     filepath.Join(".build-id", "fi", "rst"),
				Breakpad: filepath.Join("gen", "first.sym"),
				OS:       "fuchsia",
				Label:    "//first",
			},
			{
				Debug:    filepath.Join(".build-id", "fi", "rst.debug"),
				Dist:     filepath.Join(".build-id", "fi", "rst"),
				Breakpad: filepath.Join("gen", "first.sym"),
				OS:       "fuchsia",
				Label:    "//first",
			},
			{
				Debug:    filepath.Join(".build-id", "se", "cond.debug"),
				Breakpad: filepath.Join("host", "gen", "second.sym"),
				OS:       "linux",
				Label:    "//second",
			},
			{
				Debug: filepath.Join(".build-id", "th", "ird.debug"),
				Dist:  filepath.Join(".build-id", "th", "ird"),
				OS:    "linux",
				Label: "//third",
			},
		},
		pbins: []build.PrebuiltBinaries{
			{
				Name:     "prebuilt",
				Manifest: prebuiltBinManifest,
			},
		},
	}

	// The logic under test deals in file existence.
	for _, bin := range pbins {
		touch(t, filepath.Join(buildDir, bin.Debug))
		touch(t, filepath.Join(buildDir, bin.Breakpad))
	}
	for _, bin := range m.bins {
		touch(t, filepath.Join(buildDir, bin.Debug))
		if bin.Breakpad != "" {
			touch(t, filepath.Join(buildDir, bin.Breakpad))
		}
	}

	expectedUploads := []Upload{
		{
			Source:      filepath.Join(buildDir, ".build-id", "fi", "rst.debug"),
			Destination: "DEBUG_NAMESPACE/first.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "fi", "rst.debug"),
			Destination: "BUILDID_NAMESPACE/first/debuginfo",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "fi", "rst"),
			Destination: "BUILDID_NAMESPACE/first/executable",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, "gen", "first.sym"),
			Destination: "DEBUG_NAMESPACE/first.sym",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "se", "cond.debug"),
			Destination: "DEBUG_NAMESPACE/second.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "se", "cond.debug"),
			Destination: "BUILDID_NAMESPACE/second/debuginfo",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, "host", "gen", "second.sym"),
			Destination: "DEBUG_NAMESPACE/second.sym",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "th", "ird.debug"),
			Destination: "DEBUG_NAMESPACE/third.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "th", "ird.debug"),
			Destination: "BUILDID_NAMESPACE/third/debuginfo",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(buildDir, ".build-id", "th", "ird"),
			Destination: "BUILDID_NAMESPACE/third/executable",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(checkout, ".build-id", "pr", "ebuiltA.debug"),
			Destination: "DEBUG_NAMESPACE/prebuiltA.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(checkout, ".build-id", "pr", "ebuiltA.debug"),
			Destination: "BUILDID_NAMESPACE/prebuiltA/debuginfo",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(checkout, ".build-id", "pr", "ebuiltA.sym"),
			Destination: "DEBUG_NAMESPACE/prebuiltA.sym",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(checkout, ".build-id", "pr", "ebuiltB.debug"),
			Destination: "DEBUG_NAMESPACE/prebuiltB.debug",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(checkout, ".build-id", "pr", "ebuiltB.debug"),
			Destination: "BUILDID_NAMESPACE/prebuiltB/debuginfo",
			Compress:    true,
			Deduplicate: true,
		},
		{
			Source:      filepath.Join(checkout, ".build-id", "pr", "ebuiltB.sym"),
			Destination: "DEBUG_NAMESPACE/prebuiltB.sym",
			Compress:    true,
			Deduplicate: true,
		},
	}
	expectedIDs := []string{"first", "prebuiltA"}

	actualUploads, actualIDs, err := debugBinaryUploads(m, "DEBUG_NAMESPACE", "BUILDID_NAMESPACE")
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

func touch(t *testing.T, file string) {
	t.Helper()
	file, err := filepath.Abs(file)
	if err != nil {
		t.Fatalf("failed to absolutize: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(file), os.ModePerm); err != nil {
		t.Fatalf("failed to create parent directories for %s: %v", file, err)
	}
	f, err := os.Create(file)
	if err != nil {
		t.Fatalf("failed to create %s: %v", file, err)
	}
	f.Close()
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
