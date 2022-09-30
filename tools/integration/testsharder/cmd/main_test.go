// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

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
//  1. Add an entry to the `testCases` slice here.
//  2. Run `tools/integration/testsharder/update_goldens.sh` to generate the new
//     golden file.
//  3. Add the new golden file as a dependency of the test executable in
//     testsharder's BUILD.gn file.
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
		testDurations []build.TestDuration
		testList      []build.TestListEntry
		modifiers     []testsharder.TestModifier
		packageRepos  []build.PackageRepo
		affectedTests []string
	}{
		{
			name: "no tests",
		},
		{
			name: "mixed device types",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo"),
				hostTestSpec("bar"),
			},
		},
		{
			name: "multiply",
			flags: testsharderFlags{
				targetDurationSecs: 5,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo"),
				fuchsiaTestSpec("bar"),
			},
			modifiers: []testsharder.TestModifier{
				{
					Name:      "foo",
					TotalRuns: 50,
				},
				{
					Name: "bar",
				},
			},
			testDurations: []build.TestDuration{
				{
					Name:           "*",
					MedianDuration: time.Millisecond,
				},
			},
		},
		{
			name: "affected tests",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("affected-hermetic"),
				fuchsiaTestSpec("not-affected"),
			},
			testList: []build.TestListEntry{
				testListEntry("affected-hermetic", true),
				testListEntry("not-affected", false),
			},
			affectedTests: []string{
				packageURL("affected-hermetic"),
			},
		},
		{
			name: "affected nonhermetic tests",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("affected-nonhermetic"),
				fuchsiaTestSpec("not-affected"),
			},
			affectedTests: []string{
				packageURL("affected-nonhermetic"),
			},
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
		},
		{
			name: "sharding by time",
			flags: testsharderFlags{
				targetDurationSecs: int((4 * time.Minute).Seconds()),
				perTestTimeoutSecs: int((10 * time.Minute).Seconds()),
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("slow"),
				fuchsiaTestSpec("fast1"),
				fuchsiaTestSpec("fast2"),
				fuchsiaTestSpec("fast3"),
			},
			testDurations: []build.TestDuration{
				{
					Name:           "*",
					MedianDuration: 2 * time.Second,
				},
				{
					Name:           packageURL("slow"),
					MedianDuration: 5 * time.Minute,
				},
			},
		},
		{
			name: "max shards per env",
			flags: testsharderFlags{
				// Given expected test durations of 4 minutes for each test it's
				// impossible to satisfy both the target shard duration and the
				// max shards per environment, so the target shard duration
				// should effectively be ignored.
				targetDurationSecs:      int((5 * time.Minute).Seconds()),
				maxShardsPerEnvironment: 2,
				skipUnaffected:          true,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("affected1"),
				fuchsiaTestSpec("affected2"),
				fuchsiaTestSpec("affected3"),
				fuchsiaTestSpec("affected4"),
				fuchsiaTestSpec("unaffected1"),
				fuchsiaTestSpec("unaffected2"),
				fuchsiaTestSpec("nonhermetic1"),
				fuchsiaTestSpec("nonhermetic2"),
			},
			testDurations: []build.TestDuration{
				{
					Name:           "*",
					MedianDuration: 4 * time.Minute,
				},
			},
			affectedTests: []string{
				packageURL("affected1"),
				packageURL("affected2"),
				packageURL("affected3"),
				packageURL("affected4"),
			},
			testList: []build.TestListEntry{
				testListEntry("affected1", true),
				testListEntry("affected2", true),
				testListEntry("affected3", true),
				testListEntry("affected4", true),
				testListEntry("unaffected1", true),
				testListEntry("unaffected2", true),
				testListEntry("nonhermetic1", false),
				testListEntry("nonhermetic2", false),
			},
		},
		{
			name: "hermetic deps",
			flags: testsharderFlags{
				hermeticDeps: true,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo"),
				fuchsiaTestSpec("bar"),
				fuchsiaTestSpec("baz"),
			},
			packageRepos: []build.PackageRepo{
				{
					Path:    "pkg_repo1",
					Blobs:   filepath.Join("pkg_repo1", "blobs"),
					Targets: filepath.Join("pkg_repo1", "targets.json"),
				},
			},
		},
		{
			name: "multiply affected test",
			flags: testsharderFlags{
				affectedTestsMultiplyThreshold: 3,
				targetDurationSecs:             int(2 * time.Minute.Seconds()),
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("multiplied-affected-test"),
				fuchsiaTestSpec("affected-test"),
				fuchsiaTestSpec("unaffected-test"),
			},
			testDurations: []build.TestDuration{
				{
					Name:           "*",
					MedianDuration: time.Second,
				},
			},
			affectedTests: []string{
				packageURL("multiplied-affected-test"),
				packageURL("affected-test"),
			},
			modifiers: []testsharder.TestModifier{
				{
					Name:      "multiplied-affected-test",
					TotalRuns: 100,
				},
			},
		},
		{
			name: "test list with tags",
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("hermetic-test"),
				fuchsiaTestSpec("nonhermetic-test"),
			},
			testList: []build.TestListEntry{
				testListEntry("hermetic-test", true),
				testListEntry("nonhermetic-test", false),
			},
		},
		{
			name: "skip unaffected tests",
			flags: testsharderFlags{
				skipUnaffected: true,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("affected-hermetic-test"),
				fuchsiaTestSpec("unaffected-hermetic-test"),
				fuchsiaTestSpec("affected-nonhermetic-test"),
				fuchsiaTestSpec("unaffected-nonhermetic-test"),
			},
			testList: []build.TestListEntry{
				testListEntry("affected-hermetic-test", true),
				testListEntry("unaffected-hermetic-test", true),
				testListEntry("affected-nonhermetic-test", false),
				testListEntry("unaffected-nonhermetic-test", false),
			},
			affectedTests: []string{
				fuchsiaTestSpec("affected-hermetic-test").Name,
				fuchsiaTestSpec("affected-nonhermetic-test").Name,
			},
		},
		{
			name: "run all tests if no affected tests",
			flags: testsharderFlags{
				skipUnaffected: true,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("affected-hermetic-test"),
				fuchsiaTestSpec("unaffected-hermetic-test"),
				fuchsiaTestSpec("affected-nonhermetic-test"),
				fuchsiaTestSpec("unaffected-nonhermetic-test"),
			},
			testList: []build.TestListEntry{
				testListEntry("affected-hermetic-test", true),
				testListEntry("unaffected-hermetic-test", true),
				testListEntry("affected-nonhermetic-test", false),
				testListEntry("unaffected-nonhermetic-test", false),
			},
		},
		{
			name: "multiply unaffected hermetic tests",
			flags: testsharderFlags{
				skipUnaffected: true,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("unaffected-hermetic-test"),
				fuchsiaTestSpec("affected-nonhermetic-test"),
				fuchsiaTestSpec("unaffected-hermetic-multiplied-test"),
			},
			testList: []build.TestListEntry{
				testListEntry("unaffected-hermetic-test", true),
				testListEntry("affected-nonhermetic-test", false),
				testListEntry("unaffected-hermetic-multiplied-test", true),
			},
			affectedTests: []string{
				fuchsiaTestSpec("affected-nonhermetic-test").Name,
			},
			modifiers: []testsharder.TestModifier{
				{
					Name:      "unaffected-hermetic-multiplied-test",
					TotalRuns: 100,
				},
			},
		},
		{
			name: "various modifiers",
			flags: testsharderFlags{
				targetDurationSecs: 5,
			},
			testSpecs: []build.TestSpec{
				fuchsiaTestSpec("foo"),
				fuchsiaTestSpec("bar"),
				fuchsiaTestSpec("baz"),
			},
			modifiers: []testsharder.TestModifier{
				// default modifier
				{
					Name:        "*",
					TotalRuns:   -1,
					MaxAttempts: 2,
				},
				// multiplier
				{
					Name:        "foo",
					MaxAttempts: 1,
				},
				// change maxAttempts (but multiplier takes precedence)
				{
					Name:        "foo",
					TotalRuns:   -1,
					MaxAttempts: 1,
				},
				// change maxAttempts, set affected
				{
					Name:        "bar",
					Affected:    true,
					TotalRuns:   -1,
					MaxAttempts: 1,
				},
			},
			testList: []build.TestListEntry{
				testListEntry("foo", false),
				testListEntry("bar", true),
				testListEntry("baz", false),
			},
			testDurations: []build.TestDuration{
				{
					Name:           "*",
					MedianDuration: time.Millisecond,
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
				// Add a newline to the end of the file to test that it still calculates the
				// correct number of affected tests even with extra whitespace.
				tc.flags.affectedTestsPath = writeTempFile(t, strings.Join(tc.affectedTests, "\n")+"\n")
			}
			// Write test-list.json.
			if err := jsonutil.WriteToFile(
				filepath.Join(tc.flags.buildDir, testListPath),
				build.TestList{Data: tc.testList, SchemaID: "experimental"},
			); err != nil {
				t.Fatal(err)
			}
			writeDepFiles(t, tc.flags.buildDir, tc.testSpecs)
			for _, repo := range tc.packageRepos {
				if err := os.MkdirAll(filepath.Join(tc.flags.buildDir, repo.Path), 0o700); err != nil {
					t.Fatal(err)
				}
			}

			m := &fakeModules{
				testSpecs:           tc.testSpecs,
				testDurations:       tc.testDurations,
				packageRepositories: tc.packageRepos,
			}
			if err := execute(ctx, tc.flags, m); err != nil {
				t.Fatal(err)
			}

			if !*updateGoldens {
				want := readShards(t, goldenFile)
				got := readShards(t, tc.flags.outputFile)
				if diff := cmp.Diff(want, got); diff != "" {
					t.Errorf(strings.Join([]string{
						"Golden file mismatch!",
						"To fix, run `tools/integration/testsharder/update_goldens.sh",
						diff,
					}, "\n"))
				}
			}
		})
	}
}

type fakeModules struct {
	images              []build.Image
	testSpecs           []build.TestSpec
	testList            string
	testDurations       []build.TestDuration
	packageRepositories []build.PackageRepo
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

func (m *fakeModules) Images() []build.Image {
	return []build.Image{
		{
			Name: "qemu-kernel",
			Path: "multiboot.bin",
			Type: "kernel",
		},
	}
}

func (m *fakeModules) TestListLocation() []string               { return []string{testListPath} }
func (m *fakeModules) TestSpecs() []build.TestSpec              { return m.testSpecs }
func (m *fakeModules) TestDurations() []build.TestDuration      { return m.testDurations }
func (m *fakeModules) PackageRepositories() []build.PackageRepo { return m.packageRepositories }

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
			Name:            testPath,
			Path:            testPath,
			OS:              "linux",
			CPU:             "x64",
			Label:           fmt.Sprintf("//tools/other:%s(//build/toolchain/host_x64)", basename),
			RuntimeDepsFile: filepath.Join("runtime_deps", basename+".json"),
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

func testListEntry(basename string, hermetic bool) build.TestListEntry {
	return build.TestListEntry{
		Name: packageURL(basename),
		Tags: []build.TestTag{
			{Key: "hermetic", Value: strconv.FormatBool(hermetic)},
		},
	}
}

func writeDepFiles(t *testing.T, buildDir string, testSpecs []build.TestSpec) {
	// Write runtime deps files.
	for _, testSpec := range testSpecs {
		if testSpec.RuntimeDepsFile == "" {
			continue
		}
		absPath := filepath.Join(buildDir, testSpec.RuntimeDepsFile)
		if err := os.MkdirAll(filepath.Dir(absPath), 0o700); err != nil {
			t.Fatal(err)
		}
		runtimeDeps := []string{"host_x64/dep1", "host_x64/dep2"}
		if err := jsonutil.WriteToFile(absPath, runtimeDeps); err != nil {
			t.Fatal(err)
		}
	}
}

// readShards deserializes testsharder output from a JSON file.
func readShards(t *testing.T, path string) []testsharder.Shard {
	var shards []testsharder.Shard
	if err := jsonutil.ReadFromFile(path, &shards); err != nil {
		if errors.Is(err, os.ErrNotExist) && strings.HasPrefix(path, *goldensDir) {
			t.Fatalf("Golden file for case %q does not exist. To create it, run tools/integration/testsharder/update_goldens.sh", t.Name())
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
	if err := os.WriteFile(path, []byte(contents), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}
