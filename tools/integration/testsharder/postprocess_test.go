// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

func TestMultiplyShards(t *testing.T) {
	env1 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "NUC"},
		Tags:       []string{},
	}
	env3 := build.Environment{
		Dimensions: build.DimensionSet{OS: "linux"},
		Tags:       []string{},
	}
	makeTest := func(id int, os string) build.Test {
		return build.Test{
			Name: fmt.Sprintf("test%d", id),
			Path: fmt.Sprintf("/path/to/test/%d", id),
			OS:   os,
		}
	}

	shard := func(env build.Environment, os string, ids ...int) *Shard {
		var tests []build.Test
		for _, id := range ids {
			tests = append(tests, makeTest(id, os))
		}
		return &Shard{
			Name:  environmentName(env),
			Tests: tests,
			Env:   env,
		}
	}

	makeTestModifier := func(id int, os string, runs int) TestModifier {
		return TestModifier{
			Name:      fmt.Sprintf("test%d", id),
			OS:        os,
			TotalRuns: runs,
		}
	}

	multShard := func(env build.Environment, os string, id int, runs int) *Shard {
		var tests []build.Test
		test := makeTest(id, os)
		for i := 1; i <= runs; i++ {
			testCopy := test
			testCopy.Name = fmt.Sprintf("%s (%d)", test.Name, i)
			tests = append(tests, testCopy)
		}
		return &Shard{
			Name:  "multiplied:" + environmentName(env) + "-" + test.Name,
			Tests: tests,
			Env:   env,
		}
	}

	t.Run("multiply tests in shards", func(t *testing.T) {
		shards := []*Shard{
			shard(env1, "fuchsia", 1),
			shard(env2, "fuchsia", 1, 2, 4),
			shard(env3, "linux", 3),
		}
		multipliers := []TestModifier{
			makeTestModifier(1, "fuchsia", 5),
			makeTestModifier(3, "linux", 3),
		}
		actual := MultiplyShards(
			shards,
			multipliers,
		)
		expected := append(
			shards,
			// We multiplied the test with id 1 five times from the first two shards.
			multShard(env1, "fuchsia", 1, 5),
			multShard(env2, "fuchsia", 1, 5),
			multShard(env3, "linux", 3, 3),
		)
		assertEqual(t, expected, actual)
	})
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func TestWithMaxSize(t *testing.T) {
	env1 := build.Environment{
		Tags: []string{"env1"},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "env2"},
		Tags:       []string{"env2"},
	}
	input := []*Shard{namedShard(env1, "env1", 1, 2, 3, 4, 5), namedShard(env2, "env2", 6, 7, 8)}
	t.Run("does nothing if max is 0", func(t *testing.T) {
		assertEqual(t, input, WithMaxSize(input, 0))
	})
	t.Run("does nothing if max is < 0", func(t *testing.T) {
		assertEqual(t, input, WithMaxSize(input, -7))
	})
	assertShardsLessThanSize := func(t *testing.T, actual []*Shard, maxSize int) {
		for _, s := range actual {
			if len(s.Tests) > maxSize {
				t.Errorf("Shard %s has %d tests, expected at most %d", s.Name, len(s.Tests), maxSize)
			}
		}
	}
	t.Run("max is larger greater or equal to all shards", func(t *testing.T) {
		maxSize := max(len(input[0].Tests), len(input[1].Tests))
		actual := WithMaxSize(input, maxSize)
		assertEqual(t, input, actual)
		assertShardsLessThanSize(t, actual, maxSize)
	})

	t.Run("applies max", func(t *testing.T) {
		maxSize := 2
		actual := WithMaxSize(input, maxSize)
		assertEqual(t, []*Shard{
			namedShard(env1, "env1-(1)", 1, 2), namedShard(env1, "env1-(2)", 3, 4),
			namedShard(env1, "env1-(3)", 5),
			namedShard(env2, "env2-(1)", 6, 7), namedShard(env2, "env2-(2)", 8)},
			actual)
		assertShardsLessThanSize(t, actual, maxSize)
	})
	t.Run("evenly distributes tests", func(t *testing.T) {
		maxSize := 4
		actual := WithMaxSize(input, maxSize)
		assertEqual(t, []*Shard{
			namedShard(env1, "env1-(1)", 1, 2, 3), namedShard(env1, "env1-(2)", 4, 5),
			namedShard(env2, "env2", 6, 7, 8)},
			actual)
		assertShardsLessThanSize(t, actual, maxSize)
	})
}

func depsFile(t *testing.T, buildDir string, deps ...string) string {
	depsFile, err := ioutil.TempFile(buildDir, "deps")
	if err != nil {
		t.Fatal(err)
	}
	b, err := json.Marshal([]string(deps))
	if err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(depsFile.Name(), b, 0444); err != nil {
		t.Fatal(err)
	}
	relPath, err := filepath.Rel(buildDir, depsFile.Name())
	if err != nil {
		t.Fatal(err)
	}
	return relPath
}

func shardHasExpectedDeps(t *testing.T, buildDir string, tests []build.Test, expected []string) {
	shard := &Shard{
		Tests: tests,
	}
	if err := extractDepsFromShard(shard, buildDir); err != nil {
		t.Fatal(err)
	}
	if !unorderedSlicesAreEqual(shard.Deps, expected) {
		t.Fatalf("deps not as expected;\nactual:%#v\nexpected:%#v", shard.Deps, expected)
	}
}

func unorderedSlicesAreEqual(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	sort.Strings(a)
	sort.Strings(b)
	for i, v := range a {
		if v != b[i] {
			return false
		}
	}
	return true
}

func TestExtractDeps(t *testing.T) {
	buildDir, err := ioutil.TempDir("", "postprocess_test")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(buildDir)

	t.Run("no deps", func(t *testing.T) {
		tests := []build.Test{
			{
				Name: "A",
			},
		}
		expected := []string{}
		shardHasExpectedDeps(t, buildDir, tests, expected)
	})

	t.Run("some deps", func(t *testing.T) {
		tests := []build.Test{
			{
				Name:            "A",
				RuntimeDepsFile: depsFile(t, buildDir, "1", "2"),
			},
			{
				Name:            "B",
				RuntimeDepsFile: depsFile(t, buildDir, "3"),
			},
		}
		expected := []string{"1", "2", "3"}
		shardHasExpectedDeps(t, buildDir, tests, expected)

		// Also check that the depfiles have been set to empty.
		for _, test := range tests {
			if test.RuntimeDepsFile != "" {
				t.Fatalf("test %q had a nonempty RuntimeDepsFile field", test.Name)
			}
		}
	})

	t.Run("deps are deduped", func(t *testing.T) {
		tests := []build.Test{
			{
				Name:            "A",
				RuntimeDepsFile: depsFile(t, buildDir, "1", "2", "2"),
			},
			{
				Name:            "B",
				RuntimeDepsFile: depsFile(t, buildDir, "2", "3"),
			},
		}
		expected := []string{"1", "2", "3"}
		shardHasExpectedDeps(t, buildDir, tests, expected)
	})
}
