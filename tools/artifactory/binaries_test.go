// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
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
	checkout := t.TempDir()
	buildDir := filepath.Join(checkout, "out")
	if err := os.Mkdir(buildDir, 0o700); err != nil {
		t.Fatal(err)
	}

	pbins := []build.Binary{
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
	// Duplicates should be ignored.
	pbins = append(pbins, pbins[0])
	const prebuiltBinManifest = "manifest"
	writeJSON(t, filepath.Join(buildDir, prebuiltBinManifest), pbins)

	bins := []build.Binary{
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
	}
	// Duplicates should be ignored.
	bins = append(bins, bins[0])

	m := &mockBinModules{
		buildDir: buildDir,
		bins:     bins,
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

	// Mapping from each file's local filepath to the locations in GCS to which
	// the file should be uploaded.
	expectedUploadDestinations := map[string][]string{
		filepath.Join(buildDir, ".build-id", "fi", "rst.debug"): {
			"DEBUG_NAMESPACE/first.debug",
			"BUILDID_NAMESPACE/first/debuginfo",
		},
		filepath.Join(buildDir, ".build-id", "fi", "rst"): {
			"BUILDID_NAMESPACE/first/executable",
		},
		filepath.Join(buildDir, "gen", "first.sym"): {
			"DEBUG_NAMESPACE/first.sym",
			"BUILDID_NAMESPACE/first/breakpad",
		},
		filepath.Join(buildDir, ".build-id", "se", "cond.debug"): {
			"DEBUG_NAMESPACE/second.debug",
			"BUILDID_NAMESPACE/second/debuginfo",
		},
		filepath.Join(buildDir, "host", "gen", "second.sym"): {
			"DEBUG_NAMESPACE/second.sym",
			"BUILDID_NAMESPACE/second/breakpad",
		},
		filepath.Join(buildDir, ".build-id", "th", "ird.debug"): {
			"DEBUG_NAMESPACE/third.debug",
			"BUILDID_NAMESPACE/third/debuginfo",
		},
		filepath.Join(buildDir, ".build-id", "th", "ird"): {
			"BUILDID_NAMESPACE/third/executable",
		},
		filepath.Join(checkout, ".build-id", "pr", "ebuiltA.debug"): {
			"DEBUG_NAMESPACE/prebuiltA.debug",
			"BUILDID_NAMESPACE/prebuiltA/debuginfo",
		},
		filepath.Join(checkout, ".build-id", "pr", "ebuiltA.sym"): {
			"DEBUG_NAMESPACE/prebuiltA.sym",
			"BUILDID_NAMESPACE/prebuiltA/breakpad",
		},
		filepath.Join(checkout, ".build-id", "pr", "ebuiltB.debug"): {
			"DEBUG_NAMESPACE/prebuiltB.debug",
			"BUILDID_NAMESPACE/prebuiltB/debuginfo",
		},
		filepath.Join(checkout, ".build-id", "pr", "ebuiltB.sym"): {
			"DEBUG_NAMESPACE/prebuiltB.sym",
			"BUILDID_NAMESPACE/prebuiltB/breakpad",
		},
	}

	var expectedUploads []Upload
	for src, destinations := range expectedUploadDestinations {
		for _, dest := range destinations {
			expectedUploads = append(expectedUploads, Upload{
				Source:      src,
				Destination: dest,
				Compress:    true,
				Deduplicate: true,
			})
		}
	}

	expectedIDs := []string{"first", "prebuiltA"}

	actualUploads, actualIDs, err := debugBinaryUploads(m, "DEBUG_NAMESPACE", "BUILDID_NAMESPACE")
	if err != nil {
		t.Fatalf("failed to generate debug binary uploads: %v", err)
	}
	opts := cmpopts.SortSlices(func(a, b Upload) bool { return a.Destination < b.Destination })
	if diff := cmp.Diff(expectedUploads, actualUploads, opts); diff != "" {
		t.Fatalf("unexpected debug binary uploads (-want +got):\n%s", diff)
	}
	if diff := cmp.Diff(expectedIDs, actualIDs); diff != "" {
		t.Fatalf("unexpected build IDs (-want +got):\n%s", diff)
	}
}

func touch(t *testing.T, file string) {
	t.Helper()
	file, err := filepath.Abs(file)
	if err != nil {
		t.Fatalf("failed to absolutize: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(file), 0o700); err != nil {
		t.Fatalf("failed to create parent directories for %s: %v", file, err)
	}
	f, err := os.Create(file)
	if err != nil {
		t.Fatalf("failed to create %s: %v", file, err)
	}
	f.Close()
}

// writeJSON writes data as json into file named p.
func writeJSON(t *testing.T, p string, data interface{}) {
	raw, err := json.Marshal(data)
	if err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(p, raw, 0o600); err != nil {
		t.Fatal(err)
	}
}
