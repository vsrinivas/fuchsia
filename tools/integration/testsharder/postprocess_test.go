// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build"
)

func makeTest(id int, os string) Test {
	test := Test{
		Test: build.Test{
			Name: fmt.Sprintf("test%d", id),
			OS:   os,
		},
		Runs: 1,
	}
	if os == "fuchsia" {
		test.Name = fmt.Sprintf("fuchsia-pkg://fuchsia.com/test%d", id)
	} else {
		test.Name = fmt.Sprintf("/path/to/test%d", id)
	}
	return test
}

func shardWithOS(env build.Environment, os string, ids ...int) *Shard {
	var tests []Test
	for _, id := range ids {
		tests = append(tests, makeTest(id, os))
	}
	return &Shard{
		Name:  environmentName(env),
		Tests: tests,
		Env:   env,
	}
}

func affectedShard(env build.Environment, os string, ids ...int) *Shard {
	var tests []Test
	for _, id := range ids {
		tests = append(tests, makeTest(id, os))
	}
	return &Shard{
		Name:  affectedShardPrefix + environmentName(env),
		Tests: tests,
		Env:   env,
	}
}

func applyMultiplyShardTargetDurationFactor(in float64) float64 {
	// If we in-line this then the compiler treats the expression as a constant and complains
	// about implicit type conversion when we cast the product to an int. Hence this stupidly
	// simple function.
	return in * multiplyShardTargetDurationFactor
}

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

	shard := shardWithOS

	makeTestModifier := func(id int, os string, runs int) TestModifier {
		return TestModifier{
			Name:      fmt.Sprintf("test%d", id),
			OS:        os,
			TotalRuns: runs,
		}
	}

	multShard := func(env build.Environment, os string, id int, runs int) *Shard {
		test := makeTest(id, os)
		test.Runs = runs
		test.RunAlgorithm = KeepGoing
		return &Shard{
			Name:  multipliedShardPrefix + environmentName(env) + "-" + normalizeTestName(test.Name),
			Tests: []Test{test},
			Env:   env,
		}
	}

	testCases := []struct {
		name            string
		shards          []*Shard
		multipliers     []TestModifier
		testDurations   TestDurationsMap
		targetDuration  time.Duration
		targetTestCount int
		expected        []*Shard
		err             error
	}{
		{
			name: "empty os matches any os",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
				shard(env3, "linux", 1),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "", 5),
			},
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 5),
				multShard(env3, "linux", 1, 5),
			},
		},
		{
			name: "checks every shard",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
				shard(env2, "fuchsia", 1, 2, 4),
				shard(env3, "linux", 3),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "fuchsia", 5),
				makeTestModifier(3, "linux", 3),
			},
			expected: []*Shard{
				// We multiplied the test with id 1 five times from the first two shards.
				multShard(env1, "fuchsia", 1, 5),
				multShard(env2, "fuchsia", 1, 5),
				multShard(env3, "linux", 3, 3),
			},
		},
		{
			name: "fills up a shard of targetDuration if totalRuns is unset",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "fuchsia", 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: time.Second},
			},
			targetDuration: 3 * time.Second,
			expected: []*Shard{
				// The expected duration for this test is 1 second and our
				// target duration is three seconds.
				multShard(env1, "fuchsia", 1, int(applyMultiplyShardTargetDurationFactor(3.0))),
			},
		},
		{
			name: "fills up a shard of targetTestCount if totalRuns is unset",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "fuchsia", 0),
			},
			targetTestCount: 4,
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 4),
			},
		},
		{
			name: "runs defaults to 1 if no configuration values are set",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "fuchsia", 0),
			},
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 1),
			},
		},
		{
			name: "does not exceed max runs if totalRuns is unset",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "fuchsia", 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: time.Second},
			},
			targetDuration: (multipliedTestMaxRuns + 10) / multiplyShardTargetDurationFactor * time.Second,
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, multipliedTestMaxRuns),
			},
		},
		{
			name: "uses regex matches if no tests match exactly",
			shards: []*Shard{
				shard(env1, "fuchsia", 210),
			},
			multipliers: []TestModifier{
				{Name: "1", TotalRuns: 1},
			},
			expected: []*Shard{
				multShard(env1, "fuchsia", 210, 1),
			},
		},
		{
			name: "matches on test path/package URL as well as GN name",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				{Name: "fuchsia-pkg", TotalRuns: 1},
			},
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 1),
			},
		},
		{
			name: "rejects multiplier that matches too many tests",
			shards: []*Shard{
				shard(env1, "fuchsia", 10, 11, 12, 13, 14, 15),
			},
			multipliers: []TestModifier{
				makeTestModifier(1, "fuchsia", 1),
			},
			err: errTooManyMultiplierMatches,
		},
		{
			name: "rejects invalid multiplier regex",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				{Name: "["},
			},
			err: errInvalidMultiplierRegex,
		},
		{
			name: "doesn't include affected prefix in multiplied shard names",
			shards: []*Shard{
				affectedShard(env1, "fuchsia", 1),
			},
			multipliers: []TestModifier{
				{Name: "fuchsia-pkg", TotalRuns: 5},
			},
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 5),
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// `expected` includes only the multiplied shards, but we want to make
			// sure multiplication of shards leaves the original shards as-is.
			expected := append(tc.shards, tc.expected...)
			actual, err := MultiplyShards(
				tc.shards,
				tc.multipliers,
				tc.testDurations,
				tc.targetDuration,
				tc.targetTestCount,
			)
			if !errors.Is(err, tc.err) {
				t.Fatalf("got unexpected error %v, expected: %v", err, tc.err)
			}
			if err != nil {
				return
			}
			assertEqual(t, expected, actual)
		})
	}
}

func TestShardAffected(t *testing.T) {
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

	type modifyDetails struct {
		index       int
		maxAttempts int
	}

	shard := shardWithOS

	shardWithModify := func(s *Shard, md []modifyDetails) *Shard {
		for _, m := range md {
			i := m.index
			s.Tests[i].Runs = m.maxAttempts
			if m.maxAttempts > 1 {
				s.Tests[i].RunAlgorithm = StopOnSuccess
			} else {
				s.Tests[i].RunAlgorithm = ""
			}
		}
		return s
	}

	makeTestModifier := func(id int, os string, affected bool) TestModifier {
		var name string
		if os == "fuchsia" {
			name = fmt.Sprintf("fuchsia-pkg://fuchsia.com/test%d", id)
		} else {
			name = fmt.Sprintf("/path/to/test%d", id)
		}
		return TestModifier{
			Name:        name,
			OS:          os,
			Affected:    affected,
			MaxAttempts: 1,
		}
	}

	testCases := []struct {
		name      string
		shards    []*Shard
		modifiers []TestModifier
		expected  []*Shard
		err       error
	}{
		{
			name: "matches any os",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env3, "linux", 1, 2, 3),
			},
			modifiers: []TestModifier{
				{Name: "fuchsia-pkg://fuchsia.com/test3", Affected: true},
				{Name: "/path/to/test1", Affected: true},
			},
			expected: []*Shard{
				affectedShard(env1, "fuchsia", 3),
				shard(env1, "fuchsia", 1, 2),
				affectedShard(env3, "linux", 1),
				shard(env3, "linux", 2, 3),
			},
		},
		{
			name: "shards correctly",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
				shard(env1, "fuchsia", 2, 4),
				shard(env2, "fuchsia", 1, 2, 4),
				shard(env3, "linux", 3, 4),
			},
			modifiers: []TestModifier{
				makeTestModifier(1, "fuchsia", false),
				makeTestModifier(2, "fuchsia", true),
				makeTestModifier(4, "fuchsia", true),
				makeTestModifier(3, "linux", true),
			},
			expected: []*Shard{
				shard(env1, "fuchsia", 1),
				affectedShard(env1, "fuchsia", 2, 4),
				affectedShard(env2, "fuchsia", 2, 4),
				shard(env2, "fuchsia", 1),
				affectedShard(env3, "linux", 3),
				shard(env3, "linux", 4),
			},
		},
		{
			name: "copies other fields correctly",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 4, 5),
				shard(env2, "fuchsia", 1, 2, 4),
				shard(env3, "linux", 3, 4),
			},
			modifiers: []TestModifier{
				{Name: "*", MaxAttempts: 5},
				makeTestModifier(4, "fuchsia", true),
				makeTestModifier(5, "fuchsia", true),
				makeTestModifier(3, "linux", true),
			},
			expected: []*Shard{
				affectedShard(env1, "fuchsia", 4, 5),
				shardWithModify(shard(env1, "fuchsia", 1), []modifyDetails{{0, 5}}),
				affectedShard(env2, "fuchsia", 4),
				shardWithModify(shard(env2, "fuchsia", 1, 2), []modifyDetails{{0, 5}, {1, 5}}),
				affectedShard(env3, "linux", 3),
				shardWithModify(shard(env3, "linux", 4), []modifyDetails{{0, 5}}),
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			actual, err := ShardAffected(
				tc.shards,
				tc.modifiers,
			)
			if !errors.Is(err, tc.err) {
				t.Fatalf("got unexpected error %v, expected: %v", err, tc.err)
			}
			if err != nil {
				return
			}
			assertEqual(t, tc.expected, actual)
		})
	}
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// assertShardsContainTests checks that the input shards are the same as
// expectedShards, ignoring the relative ordering of the shards and the ordering
// of tests within each shard.
func assertShardsContainTests(t *testing.T, shards []*Shard, expectedShards [][]string) {
	if len(shards) != len(expectedShards) {
		t.Fatalf("shard count (%d) != expected shard count (%d)", len(shards), len(expectedShards))
	}
	for _, shard := range shards {
		actualTestNames := []string{}
		for _, test := range shard.Tests {
			for i := 0; i < test.minRequiredRuns(); i++ {
				actualTestNames = append(actualTestNames, test.Test.Name)
			}
		}

		// Check that we're expecting a shard that contains this exact set of
		// tests.
		foundMatch := false
		for i, expectedTestNames := range expectedShards {
			if stringSlicesEq(actualTestNames, expectedTestNames) {
				// Remove this expected shard so other actual shards don't get
				// matched with it.
				expectedShards = append(expectedShards[:i], expectedShards[i+1:]...)
				foundMatch = true
				break
			}
		}
		if !foundMatch {
			t.Fatalf("unexpected shard with tests %v", actualTestNames)
		}
	}
}

type runConfig struct {
	runs         int
	runAlgorithm RunAlgorithm
}

func runConfigSlicesEq(s []runConfig, t []runConfig) bool {
	if len(s) != len(t) {
		return false
	}
	seen := make(map[runConfig]int)
	for i := range s {
		seen[s[i]]++
		seen[t[i]]--
	}
	for _, v := range seen {
		if v != 0 {
			return false
		}
	}
	return true
}

func assertShardsContainRunConfigs(t *testing.T, shards []*Shard, expectedShards [][]runConfig) {
	if len(shards) != len(expectedShards) {
		t.Fatalf("shard count (%d) != expected shard count (%d)", len(shards), len(expectedShards))
	}
	for _, shard := range shards {
		actual := []runConfig{}
		for _, test := range shard.Tests {
			actual = append(actual, runConfig{test.Runs, test.RunAlgorithm})
		}

		// Check that we're expecting a shard that contains this exact set of
		// run configs.
		foundMatch := false
		for i, expected := range expectedShards {
			if runConfigSlicesEq(actual, expected) {
				// Remove this expected shard so other actual shards don't get
				// matched with it.
				expectedShards = append(expectedShards[:i], expectedShards[i+1:]...)
				foundMatch = true
				break
			}
		}
		if !foundMatch {
			t.Fatalf("unexpected shard with run config %v, expected: %v", actual, expectedShards)
		}
	}
}

func TestWithTargetDuration(t *testing.T) {
	env1 := build.Environment{
		Tags: []string{"env1"},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "env2"},
		Tags:       []string{"env2"},
	}
	defaultInput := []*Shard{namedShard(env1, "env1", 1, 2, 3, 4, 5, 6)}
	defaultDurations := TestDurationsMap{
		"*": {MedianDuration: 1},
	}

	t.Run("does nothing if test count and duration are 0", func(t *testing.T) {
		assertEqual(t, defaultInput, WithTargetDuration(defaultInput, 0, 0, 0, defaultDurations))
	})

	t.Run("does nothing if test count and duration are < 0", func(t *testing.T) {
		assertEqual(t, defaultInput, WithTargetDuration(defaultInput, -5, -7, 0, defaultDurations))
	})

	t.Run("returns one shard if target test count is greater than test count", func(t *testing.T) {
		actual := WithTargetDuration(defaultInput, 0, 20, 0, defaultDurations)
		expectedTests := [][]string{
			{"test1", "test2", "test3", "test4", "test5", "test6"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("returns one shard if target duration is greater than total duration", func(t *testing.T) {
		expectedTests := [][]string{
			{"test1", "test2", "test3", "test4", "test5", "test6"},
		}
		targetDuration := time.Duration(len(expectedTests[0]) + 1)
		actual := WithTargetDuration(defaultInput, targetDuration, 0, 0, defaultDurations)
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("obeys max-shards-per-env", func(t *testing.T) {
		input := []*Shard{namedShard(env1, "env1", 1, 2, 3)}
		maxShardsPerEnvironment := 1
		actual := WithTargetDuration(input, 1, 0, maxShardsPerEnvironment, defaultDurations)
		expectedTests := [][]string{
			{"test1", "test2", "test3"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("evenly distributes equal-duration tests", func(t *testing.T) {
		actual := WithTargetDuration(defaultInput, 4, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{"test1", "test3", "test5"},
			{"test2", "test4", "test6"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("puts long tests on their own shards", func(t *testing.T) {
		durations := TestDurationsMap{
			"*":     {MedianDuration: 1},
			"test1": {MedianDuration: 10},
		}
		actual := WithTargetDuration(defaultInput, 5, 0, 0, durations)
		expectedTests := [][]string{
			{"test1"},
			{"test2", "test3", "test4", "test5", "test6"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("uses longest test as target duration, if longer than target duration", func(t *testing.T) {
		durations := TestDurationsMap{
			"*":     {MedianDuration: 1},
			"test1": {MedianDuration: 10},
		}
		// targetDuration is 2, but the longest test's duration is 10 and we
		// can't split a single test across shards. So we must have at least one
		// shard of duration >= 10. Therefore, we won't gain anything from
		// splitting the other tests into shards of duration < 10, so they
		// should all go in the same shard, even if its duration is greater than
		// the given target.
		actual := WithTargetDuration(defaultInput, 2, 0, 0, durations)
		expectedTests := [][]string{
			{"test1"},
			{"test2", "test3", "test4", "test5", "test6"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("produces shards of similar expected durations", func(t *testing.T) {
		input := []*Shard{namedShard(env1, "env1", 1, 2, 3, 4, 5)}
		durations := TestDurationsMap{
			"test1": {MedianDuration: 1},
			"test2": {MedianDuration: 2},
			"test3": {MedianDuration: 2},
			"test4": {MedianDuration: 2},
			"test5": {MedianDuration: 5},
		}
		actual := WithTargetDuration(input, 7, 0, 0, durations)
		expectedTests := [][]string{
			{"test1", "test5"},          // total duration: 1 + 5 = 6
			{"test2", "test3", "test4"}, // total duration: 2 + 2 + 2 = 6
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("keeps different environments separate", func(t *testing.T) {
		input := []*Shard{
			namedShard(env1, "env1", 1),
			namedShard(env2, "env2", 2, 3, 4, 5, 6, 7),
		}
		actual := WithTargetDuration(input, 4, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{"test1"},
			{"test2", "test4", "test6"},
			{"test3", "test5", "test7"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("evenly divides tests even if all durations are zero", func(t *testing.T) {
		input := []*Shard{
			namedShard(env1, "env1", 1, 2, 3, 4, 5, 6),
		}
		durations := TestDurationsMap{
			"*": {MedianDuration: 0},
		}
		actual := WithTargetDuration(input, 4, 0, 2, durations)
		expectedTests := [][]string{
			{"test1", "test3", "test5"},
			{"test2", "test4", "test6"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("assigns one shard per test if all durations are zero and test count < max-shards-per-env", func(t *testing.T) {
		input := []*Shard{namedShard(env1, "env1", 1, 2, 3)}
		maxShardsPerEnvironment := 4
		durations := TestDurationsMap{
			"*": {MedianDuration: 0},
		}
		actual := WithTargetDuration(input, 1, 0, maxShardsPerEnvironment, durations)
		expectedTests := [][]string{
			{"test1"}, {"test2"}, {"test3"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("adjusts other envs if one env exceeds max shard count", func(t *testing.T) {
		var env2Tests []int
		maxShardsPerEnvironment := 8
		for i := 3; i <= 2+(maxShardsPerEnvironment*2); i++ {
			env2Tests = append(env2Tests, i)
		}
		input := []*Shard{
			namedShard(env1, "env1", 1, 2),
			namedShard(env2, "env2", env2Tests...),
		}
		actual := WithTargetDuration(input, 1, 0, maxShardsPerEnvironment, defaultDurations)
		// The subshards created for env2 must each have two tests and take
		// twice the target duration, since there are 2 *
		// maxShardsPerEnvironment tests that each take 1ns (which is the target
		// duration). So even though env1's two tests could be put on separate
		// shards without exceeding maxShardsPerEnvironment, we put them in a
		// single shard because the expected maximum duration of all shards is
		// two times the target duration anyway.
		expectedEnvs := make([]build.Environment, maxShardsPerEnvironment+1)
		expectedEnvs[0] = env1
		for i := 1; i < maxShardsPerEnvironment+1; i++ {
			expectedEnvs[i] = env2
		}
		actualEnvs := make([]build.Environment, len(actual))
		for i, shard := range actual {
			if len(shard.Tests) != 2 {
				t.Fatalf("shard doesn't have two tests: %v", shard)
			}
			actualEnvs[i] = shard.Env
		}
		if !reflect.DeepEqual(expectedEnvs, actualEnvs) {
			t.Fatalf("expected shard envs %v, got %v", actual, actualEnvs)
		}
	})

	t.Run("sorts shards by basename of first test", func(t *testing.T) {
		input := []*Shard{
			namedShard(env1, "env1", 3, 2, 1, 4, 0),
		}
		actual := WithTargetDuration(input, 1, 0, 0, defaultDurations)
		if len(actual) != len(input[0].Tests) {
			t.Fatalf("expected %d shards but got %d", len(actual), len(input[0].Tests))
		}
		for i, shard := range actual {
			expectedFirstTest := fmt.Sprintf("test%d", i)
			if len(shard.Tests) != 1 || shard.Tests[0].Name != expectedFirstTest {
				t.Fatalf("expected shard %s to contain test %s", shard.Name, expectedFirstTest)
			}
		}
	})

	t.Run("evenly distributes multiplier shards", func(t *testing.T) {
		input := []*Shard{{
			Name: "env1",
			Env:  env1,
			Tests: []Test{{
				Test: build.Test{
					Name:       "test1",
					OS:         "fuchsia",
					PackageURL: "test1",
				},
				Runs:         5,
				RunAlgorithm: KeepGoing,
			}},
		}}
		actual := WithTargetDuration(input, 2, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{"test1", "test1"},
			{"test1", "test1"},
			{"test1"},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("evenly distributes shards based on run algorithm", func(t *testing.T) {
		input := []*Shard{{
			Name: "env1",
			Env:  env1,
			Tests: []Test{
				{
					Test: build.Test{
						Name:       "test1",
						OS:         "fuchsia",
						PackageURL: "test1",
					},
					Runs:         5,
					RunAlgorithm: StopOnSuccess,
				},
				{
					Test: build.Test{
						Name:       "test2",
						OS:         "fuchsia",
						PackageURL: "test2",
					},
					Runs:         5,
					RunAlgorithm: StopOnSuccess,
				},
			}}, {
			Name: "mult",
			Env:  env1,
			Tests: []Test{
				{
					Test: build.Test{
						Name:       "test1",
						OS:         "fuchsia",
						PackageURL: "test1",
					},
					Runs:         5,
					RunAlgorithm: KeepGoing,
				},
			}},
		}
		actual := WithTargetDuration(input, 2, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{"test1", "test2"},
			{"test1", "test1"},
			{"test1", "test1"},
			{"test1"},
		}
		expectedRuns := [][]runConfig{
			{{5, StopOnSuccess}, {5, StopOnSuccess}},
			{{2, KeepGoing}},
			{{2, KeepGoing}},
			{{1, KeepGoing}}}
		assertShardsContainRunConfigs(t, actual, expectedRuns)
		assertShardsContainTests(t, actual, expectedTests)
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

func shardHasExpectedDeps(t *testing.T, buildDir string, tests []Test, expected []string) {
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
		tests := []Test{{
			Test: build.Test{
				Name: "A",
			},
		}}
		expected := []string{}
		shardHasExpectedDeps(t, buildDir, tests, expected)
	})

	t.Run("some deps", func(t *testing.T) {
		tests := []Test{{
			Test: build.Test{
				Name:            "A",
				RuntimeDepsFile: depsFile(t, buildDir, "1", "2"),
			}}, {
			Test: build.Test{
				Name:            "B",
				RuntimeDepsFile: depsFile(t, buildDir, "3"),
			}},
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
		tests := []Test{{
			Test: build.Test{
				Name:            "A",
				RuntimeDepsFile: depsFile(t, buildDir, "1", "2", "2"),
			}}, {
			Test: build.Test{
				Name:            "B",
				RuntimeDepsFile: depsFile(t, buildDir, "2", "3"),
			}},
		}
		expected := []string{"1", "2", "3"}
		shardHasExpectedDeps(t, buildDir, tests, expected)
	})

	t.Run("host test paths are added to deps", func(t *testing.T) {
		tests := []Test{
			{
				Test: build.Test{
					Name:            "A",
					OS:              "linux",
					Path:            "path/to/A",
					RuntimeDepsFile: depsFile(t, buildDir, "1"),
				},
			},
			{
				Test: build.Test{
					Name:            "B",
					OS:              "mac",
					Path:            "path/to/B",
					RuntimeDepsFile: depsFile(t, buildDir, "2"),
				},
			},
		}
		expected := []string{"1", "2", "path/to/A", "path/to/B"}
		shardHasExpectedDeps(t, buildDir, tests, expected)
	})
}
