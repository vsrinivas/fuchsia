// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

var (
	updateGoldens = flag.Bool("update-goldens", false, "Whether to update goldens")
	goldensDir    = flag.String("goldens-dir", "testdata", "Directory containing goldens")
)

const testListPath = "fake-test-list.json"

// TestExecute runs golden tests for the execute() function.
//
// To add a new test case:
//   1. Add an entry to the `testCases` slice here.
//   2. Run `tools/integration/testsharder/update_goldens.sh` to generate the new
//      golden file.
//   3. Add the new golden file as a dependency of the test executable in
//      testsharder's BUILD.gn file.
func TestExecute(t *testing.T) {
	ctx := context.Background()

	// Clear pre-existing golden files to avoid leaving stale ones around.
	if *updateGoldens {
		files, err := filepath.Glob(filepath.Join(*goldensDir, "*.golden.json"))
		if err != nil {
			t.Fatal(err)
		}
		for _, f := range files {
			if err := os.Remove(f); err != nil {
				t.Fatal(err)
			}
		}
	}

	testCases := []struct {
		name          string
		flags         testsharderFlags
		testSpecs     []build.TestSpec
		testList      []build.TestListEntry
		modifiers     []testsharder.TestModifier
		affectedTests []string
	}{
		{
			name: "basic",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo"),
				hostTestSpec("bar"),
			},
		},
		{
			name: "multiply",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo"),
			},
			modifiers: []testsharder.TestModifier{
				{
					Name:      "foo",
					TotalRuns: 50,
				},
			},
		},
		{
			name: "affected tests",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("affected"),
				fuchsiaTestSpec("not-affected"),
			},
			affectedTests: []string{fuchsiaTestSpec("affected").Name},
		},
		{
			name: "target test count",
			flags: testsharderFlags{
				targetTestCount: 2,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo1"),
				fuchsiaTestSpec("foo2"),
				fuchsiaTestSpec("foo3"),
				fuchsiaTestSpec("foo4"),
			},
			affectedTests: []string{"foo"},
		},
		{
			name: "test list with tags",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("hermetic-test"),
				fuchsiaTestSpec("nonhermetic-test"),
			},
			testList: []build.TestListEntry{
				{
					Name: packageURL("hermetic-test"),
					Tags: []build.TestTag{
						{Key: "hermetic", Value: "true"},
					},
				},
				{
					Name: packageURL("nonhermetic-test"),
					Tags: []build.TestTag{
						{Key: "hermetic", Value: "false"},
					},
				},
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			goldenBasename := strings.ReplaceAll(tc.name, " ", "_") + ".golden.json"
			goldenFile := filepath.Join(*goldensDir, goldenBasename)

			if *updateGoldens {
				tc.flags.outputFile = goldenFile
			} else {
				tc.flags.outputFile = filepath.Join(t.TempDir(), goldenBasename)
			}

			tc.flags.buildDir = t.TempDir()
			if len(tc.modifiers) > 0 {
				tc.flags.modifiersPath = writeTempJSONFile(t, tc.modifiers)
			}
			if len(tc.affectedTests) > 0 {
				tc.flags.affectedTestsPath = writeTempFile(t, strings.Join(tc.affectedTests, "\n"))
			}
			if err := jsonutil.WriteToFile(
				filepath.Join(tc.flags.buildDir, testListPath),
				build.TestList{Tests: tc.testList},
			); err != nil {
				t.Fatal(err)
			}

			m := &fakeModules{testSpecs: tc.testSpecs}
			if err := execute(ctx, tc.flags, m); err != nil {
				t.Fatal(err)
			}

			if !*updateGoldens {
				want := readShards(t, goldenFile)
				got := readShards(t, tc.flags.outputFile)
				if diff := cmp.Diff(want, got); diff != "" {
					t.Errorf(strings.Join([]string{
						"Golden file mismatch!",
						"To fix, run `tools/integration/testsharder/update_golden.sh",
						diff,
					}, "\n"))
				}
			}
		})
	}
}

type fakeModules struct {
	images        []build.Image
	testSpecs     []build.TestSpec
	testList      string
	testDurations []build.TestDuration
}

func (m *fakeModules) Platforms() []build.DimensionSet {
	return []build.DimensionSet{
		{
			DeviceType: "AEMU",
		},
		{
			CPU: "x64",
			OS:  "Linux",
		},
	}
}
func (m *fakeModules) TestListLocation() []string          { return []string{testListPath} }
func (m *fakeModules) Images() []build.Image               { return m.images }
func (m *fakeModules) TestSpecs() []build.TestSpec         { return m.testSpecs }
func (m *fakeModules) TestDurations() []build.TestDuration { return m.testDurations }

func packageURL(basename string) string {
	return fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s#meta/%s.cm", basename, basename)
}

func fuchsiaTestSpec(basename string) build.TestSpec {
	return build.TestSpec{
		Test: build.Test{
			Name:       packageURL(basename),
			PackageURL: packageURL(basename),
			OS:         "fuchsia",
			CPU:        "x64",
			Label:      fmt.Sprintf("//src/something:%s(//build/toolchain/fuchsia:x64)", basename),
		},
		Envs: []build.Environment{
			{
				Dimensions: build.DimensionSet{
					DeviceType: "AEMU",
				},
				IsEmu: true,
			},
		},
	}
}

func hostTestSpec(basename string) build.TestSpec {
	testPath := fmt.Sprintf("host_x64/%s", basename)
	return build.TestSpec{
		Test: build.Test{
			Name:  testPath,
			Path:  testPath,
			OS:    "linux",
			CPU:   "x64",
			Label: fmt.Sprintf("//tools/other:%s(//build/toolchain/host_x64)", basename),
		},
		Envs: []build.Environment{
			{
				Dimensions: build.DimensionSet{
					CPU: "x64",
					OS:  "Linux",
				},
			},
		},
	}
}

// readShards deserializes testsharder output from a JSON file.
func readShards(t *testing.T, path string) []testsharder.Shard {
	var shards []testsharder.Shard
	if err := jsonutil.ReadFromFile(path, &shards); err != nil {
		if errors.Is(err, os.ErrNotExist) && strings.HasPrefix(path, *goldensDir) {
			t.Fatalf("Golden file %s has not been created yet", path)
		}
		t.Fatal(err)
	}
	return shards
}

func writeTempJSONFile(t *testing.T, obj interface{}) string {
	path := filepath.Join(t.TempDir(), "temp.json")
	if err := jsonutil.WriteToFile(path, obj); err != nil {
		t.Fatal(err)
	}
	return path
}

func writeTempFile(t *testing.T, contents string) string {
	path := filepath.Join(t.TempDir(), "temp.txt")
	if err := ioutil.WriteFile(path, []byte(contents), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}
