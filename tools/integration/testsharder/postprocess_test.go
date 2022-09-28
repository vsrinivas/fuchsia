// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func affectedShard(env build.Environment, os string, ids ...int) *Shard {
	var tests []Test
	for _, id := range ids {
		test := makeTest(id, os)
		test.Affected = true
		tests = append(tests, test)
	}
	return &Shard{
		Name:  AffectedShardPrefix + environmentName(env),
		Tests: tests,
		Env:   env,
	}
}

func TestSplitOutMultipliers(t *testing.T) {
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

	makeModifierMatch := func(id int, env build.Environment, runs int) ModifierMatch {
		os := "fuchsia"
		if !envsEqual(env, build.Environment{}) && env.Dimensions.OS != "" {
			os = env.Dimensions.OS
		}
		return ModifierMatch{
			Test: fullTestName(id, os),
			Env:  env,
			Modifier: TestModifier{
				TotalRuns: runs,
			},
		}
	}

	multShard := func(env build.Environment, os string, runs, timeoutSecs int, ids ...int) *Shard {
		tests := []Test{}
		for _, id := range ids {
			test := makeTest(id, os)
			test.Runs = runs
			test.RunAlgorithm = StopOnFailure
			test.StopRepeatingAfterSecs = int(timeoutSecs / len(ids))
			tests = append(tests, test)
		}
		return &Shard{
			Name:        MultipliedShardPrefix + environmentName(env),
			Tests:       tests,
			Env:         env,
			TimeoutSecs: int(computeShardTimeout(subshard{time.Duration(timeoutSecs) * time.Second, tests}).Seconds()),
		}
	}

	multShardWithIndex := func(env build.Environment, os string, runs, timeoutSecs, shardIndex int, ids ...int) *Shard {
		shard := multShard(env, os, runs, timeoutSecs, ids...)
		shard.Name = fmt.Sprintf("%s-(%d)", shard.Name, shardIndex)
		return shard
	}

	withStopRepeatingAfterSecs := func(shard *Shard, stopRepeatingAfterSecs int) *Shard {
		for i := range shard.Tests {
			shard.Tests[i].StopRepeatingAfterSecs = stopRepeatingAfterSecs
		}
		return shard
	}

	affectedMultShard := func(env build.Environment, os string, runs, timeoutSecs int, ids ...int) *Shard {
		shard := multShard(env, os, runs, timeoutSecs, ids...)
		shard.Name = MultipliedShardPrefix + AffectedShardPrefix + strings.TrimPrefix(shard.Name, MultipliedShardPrefix)
		for i := range shard.Tests {
			shard.Tests[i].Affected = true
		}
		return shard
	}

	testCases := []struct {
		name            string
		shards          []*Shard
		multipliers     []ModifierMatch
		testDurations   TestDurationsMap
		targetDuration  time.Duration
		targetTestCount int
		expected        []*Shard
	}{
		{
			name: "empty env matches any env",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
				shard(env2, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, build.Environment{}, 5),
			},
			expected: []*Shard{
				multShard(env2, "fuchsia", 5, 0, 1),
				multShard(env1, "fuchsia", 5, 0, 1),
			},
		},
		{
			name: "checks every shard",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
				shard(env2, "fuchsia", 1, 2, 4),
				shard(env3, "linux", 3),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 5),
				makeModifierMatch(1, env2, 5),
				makeModifierMatch(3, env3, 3),
			},
			expected: []*Shard{
				shard(env2, "fuchsia", 2, 4),
				// We multiplied the test with id 1 five times from the first two shards.
				multShard(env3, "linux", 3, 0, 3),
				multShard(env2, "fuchsia", 5, 0, 1),
				multShard(env1, "fuchsia", 5, 0, 1),
			},
		},
		{
			name: "fills up a shard of targetDuration if totalRuns is unset",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: time.Second},
			},
			targetDuration: 3 * time.Second,
			expected: []*Shard{
				// The expected duration for this test is 1 second and our
				// target duration is three seconds.
				multShard(env1, "fuchsia", 3, 3, 1),
			},
		},
		{
			name: "fills up a shard of targetTestCount if totalRuns is unset",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
			},
			targetTestCount: 4,
			expected: []*Shard{
				multShard(env1, "fuchsia", 4, 0, 1),
			},
		},
		{
			name: "runs defaults to 1 if no configuration values are set",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
			},
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 0, 1),
			},
		},
		{
			name: "does not exceed max runs if totalRuns is unset",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: time.Second},
			},
			targetDuration: (multipliedTestMaxRuns + 10) * time.Second,
			expected: []*Shard{
				multShard(env1, "fuchsia", multipliedTestMaxRuns, multipliedTestMaxRuns+10, 1),
			},
		},
		{
			name: "runs at least once even if duration is longer than target duration",
			shards: []*Shard{
				shard(env1, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 10 * time.Second},
			},
			targetDuration: 2 * time.Second,
			expected: []*Shard{
				multShard(env1, "fuchsia", 1, 2, 1),
			},
		},
		{
			name: "doesn't include affected prefix in multiplied shard names",
			shards: []*Shard{
				affectedShard(env1, "fuchsia", 1),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, build.Environment{}, 5),
			},
			expected: []*Shard{
				affectedMultShard(env1, "fuchsia", 5, 0, 1),
			},
		},
		{
			name: "removes multiplied test from shard",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 2),
			},
			expected: []*Shard{
				shard(env1, "fuchsia", 2),
				multShard(env1, "fuchsia", 2, 0, 1),
			},
		},
		{
			name: "fill up multiple tests in max shards",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3, 4, 5),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
				makeModifierMatch(2, env1, 0),
				makeModifierMatch(3, env1, 0),
				makeModifierMatch(4, env1, 0),
				makeModifierMatch(5, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 1 * time.Second},
			},
			targetDuration: 10 * time.Second,
			expected: []*Shard{
				multShardWithIndex(env1, "fuchsia", 10, 10, 1, 1),
				multShardWithIndex(env1, "fuchsia", 5, 10, 2, 4, 2),
				multShardWithIndex(env1, "fuchsia", 5, 10, 3, 5, 3),
			},
		},
		{
			name: "max tests in fewer shards",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3, 4, 5, 6),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
				makeModifierMatch(2, env1, 0),
				makeModifierMatch(3, env1, 0),
				makeModifierMatch(4, env1, 0),
				makeModifierMatch(5, env1, 0),
				makeModifierMatch(6, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 1 * time.Millisecond},
			},
			targetDuration: 10 * time.Second,
			expected: []*Shard{
				multShard(env1, "fuchsia", 1000, 10, 4, 5, 6, 1, 2, 3),
			},
		},
		{
			name: "max tests split evenly",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3, 4, 5, 6),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
				makeModifierMatch(2, env1, 0),
				makeModifierMatch(3, env1, 0),
				makeModifierMatch(4, env1, 0),
				makeModifierMatch(5, env1, 0),
				makeModifierMatch(6, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 5 * time.Millisecond},
			},
			targetDuration: 10 * time.Second,
			expected: []*Shard{
				multShardWithIndex(env1, "fuchsia", 1000, 10, 1, 6, 1),
				multShardWithIndex(env1, "fuchsia", 1000, 10, 2, 4, 2),
				multShardWithIndex(env1, "fuchsia", 1000, 10, 3, 5, 3),
			},
		},
		{
			name: "max tests split unevenly",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3, 4),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 0),
				makeModifierMatch(2, env1, 0),
				makeModifierMatch(3, env1, 0),
				makeModifierMatch(4, env1, 0),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 7 * time.Millisecond},
			},
			targetDuration: 10 * time.Second,
			expected: []*Shard{
				multShardWithIndex(env1, "fuchsia", 1000, 10, 1, 1),
				multShardWithIndex(env1, "fuchsia", 715, 10, 2, 4, 2),
				multShardWithIndex(env1, "fuchsia", 1000, 10, 3, 3),
			},
		},
		{
			name: "run all total runs regardless of target duration",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3, 4, 5, 6),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 500),
				makeModifierMatch(2, env1, 500),
				makeModifierMatch(3, env1, 500),
				makeModifierMatch(4, env1, 500),
				makeModifierMatch(5, env1, 500),
				makeModifierMatch(6, env1, 500),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 1 * time.Second},
			},
			targetDuration: 10 * time.Second,
			expected: []*Shard{
				withStopRepeatingAfterSecs(multShardWithIndex(env1, "fuchsia", 500, 10, 1, 6, 1), 10),
				withStopRepeatingAfterSecs(multShardWithIndex(env1, "fuchsia", 500, 10, 2, 4, 2), 10),
				withStopRepeatingAfterSecs(multShardWithIndex(env1, "fuchsia", 500, 10, 3, 5, 3), 10),
			},
		},
		{
			name: "only run each multiplier test in one shard",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 50),
				makeModifierMatch(2, env1, 50),
			},
			testDurations: TestDurationsMap{
				"*": {MedianDuration: 1 * time.Second},
			},
			targetDuration: 10 * time.Second,
			expected: []*Shard{
				multShardWithIndex(env1, "fuchsia", 50, 10, 1, 1),
				multShardWithIndex(env1, "fuchsia", 50, 10, 2, 2),
			},
		},
		{
			name: "choose less runs if multiple multipliers for same test",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2),
			},
			multipliers: []ModifierMatch{
				makeModifierMatch(1, env1, 3),
				makeModifierMatch(1, env1, 2),
				makeModifierMatch(1, env1, 5),
			},
			expected: []*Shard{
				shard(env1, "fuchsia", 2),
				multShard(env1, "fuchsia", 2, 0, 1),
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			shards, err := ApplyModifiers(tc.shards, tc.multipliers)
			if err != nil {
				t.Fatalf("got unexpected error: %s", err)
			}
			actual := SplitOutMultipliers(
				context.Background(),
				shards,
				tc.testDurations,
				tc.targetDuration,
				tc.targetTestCount,
				MultipliedShardPrefix,
			)
			assertEqual(t, tc.expected, actual)
		})
	}
}

func TestAddExpectedDurationTags(t *testing.T) {
	env1 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{OS: "linux"},
		Tags:       []string{},
	}
	shardWithDurations := func(s *Shard, durations ...time.Duration) *Shard {
		var newTests []Test
		for i, test := range s.Tests {
			test.Tags = append(test.Tags, build.TestTag{
				Key:   expectedDurationTagKey,
				Value: strconv.FormatInt(durations[i].Milliseconds(), 10),
			})
			newTests = append(newTests, test)
		}
		s.Tests = newTests
		return s
	}
	durations := TestDurationsMap{
		fullTestName(1, "fuchsia"): build.TestDuration{
			Name:           fullTestName(1, "fuchsia"),
			MedianDuration: 1 * time.Second,
		},
		fullTestName(2, "fuchsia"): build.TestDuration{
			Name:           fullTestName(1, "fuchsia"),
			MedianDuration: 2 * time.Second,
		},
		fullTestName(3, "fuchsia"): build.TestDuration{
			Name:           fullTestName(1, "fuchsia"),
			MedianDuration: 3 * time.Second,
		},
		fullTestName(1, "linux"): build.TestDuration{
			Name:           fullTestName(1, "linux"),
			MedianDuration: 4 * time.Second,
		},
		fullTestName(2, "linux"): build.TestDuration{
			Name:           fullTestName(1, "linux"),
			MedianDuration: 5 * time.Second,
		},
		fullTestName(3, "linux"): build.TestDuration{
			Name:           fullTestName(1, "linux"),
			MedianDuration: 6 * time.Second,
		},
	}
	want := []*Shard{
		shardWithDurations(shard(env1, "fuchsia", 1, 2, 3), 1*time.Second, 2*time.Second, 3*time.Second),
		shardWithDurations(shard(env2, "linux", 1, 2, 3), 4*time.Second, 5*time.Second, 6*time.Second),
	}
	got := AddExpectedDurationTags(
		[]*Shard{
			shard(env1, "fuchsia", 1, 2, 3),
			shard(env2, "linux", 1, 2, 3),
		},
		durations,
	)
	assertEqual(t, want, got)
}

func TestApplyModifiers(t *testing.T) {
	env1 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{OS: "linux"},
		Tags:       []string{},
	}

	makeModifierMatch := func(id int, env build.Environment, affected bool, maxAttempts, totalRuns int) ModifierMatch {
		os := "fuchsia"
		if env.Dimensions.OS != "" {
			os = env.Dimensions.OS
		}
		return ModifierMatch{
			Test: fullTestName(id, os),
			Env:  env,
			Modifier: TestModifier{
				Affected:    affected,
				TotalRuns:   totalRuns,
				MaxAttempts: maxAttempts,
			},
		}
	}

	type modifyDetails struct {
		index        int
		affected     bool
		runs         int
		runAlgorithm RunAlgorithm
	}

	makeModifyDetails := func(runs int, runAlgorithm RunAlgorithm, affected bool, ids ...int) []modifyDetails {
		var details []modifyDetails
		for _, id := range ids {
			details = append(details, modifyDetails{id, affected, runs, runAlgorithm})
		}
		return details
	}

	shardWithModify := func(s *Shard, md []modifyDetails) *Shard {
		for _, m := range md {
			i := m.index
			s.Tests[i].Runs = m.runs
			s.Tests[i].RunAlgorithm = m.runAlgorithm
			if m.affected {
				s.Tests[i].Affected = true
			}
		}
		return s
	}

	testCases := []struct {
		name      string
		shards    []*Shard
		modifiers []ModifierMatch
		expected  []*Shard
		err       error
	}{
		{
			name: "no modifiers provided",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			expected: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name: "default modifier for specific env",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				{Env: env1, Modifier: TestModifier{Name: "*", OS: "fuchsia", TotalRuns: -1, MaxAttempts: 2}},
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(2, StopOnSuccess, false, 0, 1, 2)),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name: "default modifier for all envs",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				{Modifier: TestModifier{Name: "*", TotalRuns: -1, MaxAttempts: 2}},
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(2, StopOnSuccess, false, 0, 1, 2)),
				shardWithModify(shard(env2, "linux", 1, 2, 3), makeModifyDetails(2, StopOnSuccess, false, 0, 1, 2)),
			},
		},
		{
			name: "multiple default modifiers",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				{Modifier: TestModifier{Name: "*", TotalRuns: -1, Affected: true}},
				{Modifier: TestModifier{Name: "*", TotalRuns: -1, Affected: true}},
			},
			err: errMultipleDefaultModifiers,
		},
		{
			name: "single modifier for a single test",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				makeModifierMatch(1, env1, true, 0, -1),
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(1, "", true, 0)),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name: "multiple modifiers for a single test",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				makeModifierMatch(1, env1, true, 0, -1),
				makeModifierMatch(1, env1, false, 5, -1),
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(5, StopOnSuccess, true, 0)),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name: "multiple modifiers for multiple tests",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				makeModifierMatch(1, env1, true, 0, -1),
				makeModifierMatch(2, env1, true, 0, -1),
				makeModifierMatch(1, env2, false, 5, -1),
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(1, "", true, 0, 1)),
				shardWithModify(shard(env2, "linux", 1, 2, 3), makeModifyDetails(5, StopOnSuccess, false, 0)),
			},
		},
		{
			name: "total runs takes precedence over max attempts",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				makeModifierMatch(1, env1, true, 0, 10),
				makeModifierMatch(1, env1, false, 5, -1),
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(10, StopOnFailure, true, 0)),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name: "choose specified total runs over unspecified",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				makeModifierMatch(1, env1, true, 0, 10),
				makeModifierMatch(1, env1, false, 0, 0),
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(10, StopOnFailure, true, 0)),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name: "choose less runs",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			modifiers: []ModifierMatch{
				makeModifierMatch(1, env1, true, 0, 10),
				makeModifierMatch(1, env1, false, 0, 5),
			},
			expected: []*Shard{
				shardWithModify(shard(env1, "fuchsia", 1, 2, 3), makeModifyDetails(5, StopOnFailure, true, 0)),
				shard(env2, "linux", 1, 2, 3),
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			actual, err := ApplyModifiers(
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

func TestPartitionShards(t *testing.T) {
	env1 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "NUC"},
		Tags:       []string{},
	}
	shardWithAffected := func(s *Shard, affectedIndices ...int) *Shard {
		for _, i := range affectedIndices {
			s.Tests[i].Affected = true
		}
		return s
	}

	testCases := []struct {
		name               string
		shards             []*Shard
		partitionFunc      func(Test) bool
		prefix             string
		expectedPartition1 []*Shard
		expectedPartition2 []*Shard
	}{
		{
			name:   "partitions correctly",
			prefix: AffectedShardPrefix,
			shards: []*Shard{
				shardWithAffected(shard(env1, "fuchsia", 1, 2, 3), 2),
				shardWithAffected(shard(env2, "linux", 1, 2, 3), 0),
			},
			partitionFunc: func(t Test) bool {
				return t.Affected
			},
			expectedPartition1: []*Shard{
				affectedShard(env1, "fuchsia", 3),
				affectedShard(env2, "linux", 1),
			},
			expectedPartition2: []*Shard{
				shard(env1, "fuchsia", 1, 2),
				shard(env2, "linux", 2, 3),
			},
		},
		{
			name:   "shards with no matching tests return only nonmatching shards",
			prefix: AffectedShardPrefix,
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			partitionFunc: func(t Test) bool {
				return t.Affected
			},
			expectedPartition1: []*Shard{},
			expectedPartition2: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
		},
		{
			name:   "shards with only matching tests return only matching shards",
			prefix: AffectedShardPrefix,
			shards: []*Shard{
				shardWithAffected(shard(env1, "fuchsia", 1, 2, 3), 0, 1, 2),
				shardWithAffected(shard(env2, "linux", 1, 2, 3), 0, 1, 2),
			},
			partitionFunc: func(t Test) bool {
				return t.Affected
			},
			expectedPartition1: []*Shard{
				affectedShard(env1, "fuchsia", 1, 2, 3),
				affectedShard(env2, "linux", 1, 2, 3),
			},
			expectedPartition2: []*Shard{},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			partition1, partition2 := PartitionShards(
				tc.shards,
				tc.partitionFunc,
				tc.prefix,
			)
			assertEqual(t, tc.expectedPartition1, partition1)
			assertEqual(t, tc.expectedPartition2, partition2)
		})
	}
}

func TestMarkShardsSkipped(t *testing.T) {
	env1 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "QEMU"},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "NUC"},
	}

	summary := func(os string, ids ...int) runtests.TestSummary {
		var s runtests.TestSummary
		for _, id := range ids {
			test := makeTest(id, os)
			s.Tests = append(s.Tests, runtests.TestDetails{
				Name:    test.Name,
				GNLabel: test.Label,
				Result:  runtests.TestSkipped,
				Tags:    test.Tags,
			})
		}
		return s
	}

	skippedShard := func(s *Shard, summary runtests.TestSummary) *Shard {
		s.Summary = summary
		return s
	}

	shardWithAffected := func(s *Shard, affectedIndices ...int) *Shard {
		for _, i := range affectedIndices {
			s.Tests[i].Affected = true
		}
		return s
	}

	testCases := []struct {
		name     string
		shards   []*Shard
		expected []*Shard
		err      error
	}{
		{
			name: "shards are skipped correctly",
			shards: []*Shard{
				shard(env1, "fuchsia", 1, 2, 3),
				shard(env2, "linux", 1, 2, 3),
			},
			expected: []*Shard{
				skippedShard(shard(env1, "fuchsia", 1, 2, 3), summary("fuchsia", 1, 2, 3)),
				skippedShard(shard(env2, "linux", 1, 2, 3), summary("linux", 1, 2, 3)),
			},
		},
		{
			name: "skipping an affected test throws an error",
			shards: []*Shard{
				shardWithAffected(shard(env1, "fuchsia", 1, 2, 3), 0),
				shard(env2, "linux", 1, 2, 3),
			},
			err: errSkippedAffectedTest,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := MarkShardsSkipped(tc.shards)
			if !errors.Is(err, tc.err) {
				t.Fatalf("got unexpected error %v, expected: %v", err, tc.err)
			}
			if err != nil {
				return
			}
			assertEqual(t, tc.expected, got)
		})
	}
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
		Dimensions: build.DimensionSet{DeviceType: "env1"},
		Tags:       []string{"env1"},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "env2"},
		Tags:       []string{"env2"},
	}
	defaultInput := []*Shard{shard(env1, "fuchsia", 1, 2, 3, 4, 5, 6)}
	defaultDurations := TestDurationsMap{
		"*": {MedianDuration: 1},
	}

	test := func(id int) string {
		return fullTestName(id, "fuchsia")
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
			{test(1), test(2), test(3), test(4), test(5), test(6)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("returns one shard if target duration is greater than total duration", func(t *testing.T) {
		expectedTests := [][]string{
			{test(1), test(2), test(3), test(4), test(5), test(6)},
		}
		targetDuration := time.Duration(len(expectedTests[0]) + 1)
		actual := WithTargetDuration(defaultInput, targetDuration, 0, 0, defaultDurations)
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("obeys max-shards-per-env", func(t *testing.T) {
		input := []*Shard{shard(env1, "fuchsia", 1, 2, 3)}
		maxShardsPerEnvironment := 1
		actual := WithTargetDuration(input, 1, 0, maxShardsPerEnvironment, defaultDurations)
		expectedTests := [][]string{
			{test(1), test(2), test(3)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("evenly distributes equal-duration tests", func(t *testing.T) {
		actual := WithTargetDuration(defaultInput, 4, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{test(1), test(3), test(5)},
			{test(2), test(4), test(6)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("puts long tests on their own shards", func(t *testing.T) {
		durations := TestDurationsMap{
			"*":     {MedianDuration: 1},
			test(1): {MedianDuration: 10},
		}
		actual := WithTargetDuration(defaultInput, 5, 0, 0, durations)
		expectedTests := [][]string{
			{test(1)},
			{test(2), test(3), test(4), test(5), test(6)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("uses longest test as target duration, if longer than target duration", func(t *testing.T) {
		durations := TestDurationsMap{
			"*":     {MedianDuration: 1},
			test(1): {MedianDuration: 10},
		}
		// targetDuration is 2, but the longest test's duration is 10 and we
		// can't split a single test across shards. So we must have at least one
		// shard of duration >= 10. Therefore, we won't gain anything from
		// splitting the other tests into shards of duration < 10, so they
		// should all go in the same shard, even if its duration is greater than
		// the given target.
		actual := WithTargetDuration(defaultInput, 2, 0, 0, durations)
		expectedTests := [][]string{
			{test(1)},
			{test(2), test(3), test(4), test(5), test(6)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("produces shards of similar expected durations", func(t *testing.T) {
		input := []*Shard{shard(env1, "fuchsia", 1, 2, 3, 4, 5)}
		durations := TestDurationsMap{
			test(1): {MedianDuration: 1},
			test(2): {MedianDuration: 2},
			test(3): {MedianDuration: 2},
			test(4): {MedianDuration: 2},
			test(5): {MedianDuration: 5},
		}
		actual := WithTargetDuration(input, 7, 0, 0, durations)
		expectedTests := [][]string{
			{test(1), test(5)},          // total duration: 1 + 5 = 6
			{test(2), test(3), test(4)}, // total duration: 2 + 2 + 2 = 6
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("keeps different environments separate", func(t *testing.T) {
		input := []*Shard{
			shard(env1, "fuchsia", 1),
			shard(env2, "fuchsia", 2, 3, 4, 5, 6, 7),
		}
		actual := WithTargetDuration(input, 4, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{test(1)},
			{test(2), test(4), test(6)},
			{test(3), test(5), test(7)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("evenly divides tests even if all durations are zero", func(t *testing.T) {
		input := []*Shard{
			shard(env1, "fuchsia", 1, 2, 3, 4, 5, 6),
		}
		durations := TestDurationsMap{
			"*": {MedianDuration: 0},
		}
		actual := WithTargetDuration(input, 4, 0, 2, durations)
		expectedTests := [][]string{
			{test(1), test(3), test(5)},
			{test(2), test(4), test(6)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("assigns one shard per test if all durations are zero and test count < max-shards-per-env", func(t *testing.T) {
		input := []*Shard{shard(env1, "fuchsia", 1, 2, 3)}
		maxShardsPerEnvironment := 4
		durations := TestDurationsMap{
			"*": {MedianDuration: 0},
		}
		actual := WithTargetDuration(input, 1, 0, maxShardsPerEnvironment, durations)
		expectedTests := [][]string{
			{test(1)}, {test(2)}, {test(3)},
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
			shard(env1, "fuchsia", 1, 2),
			shard(env2, "fuchsia", env2Tests...),
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
			shard(env1, "fuchsia", 3, 2, 1, 4, 0),
		}
		actual := WithTargetDuration(input, 1, 0, 0, defaultDurations)
		if len(actual) != len(input[0].Tests) {
			t.Fatalf("expected %d shards but got %d", len(actual), len(input[0].Tests))
		}
		for i, shard := range actual {
			expectedFirstTest := fullTestName(i, "fuchsia")
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
				Test:         makeTest(1, "fuchsia").Test,
				Runs:         5,
				RunAlgorithm: KeepGoing,
			}},
		}}
		actual := WithTargetDuration(input, 2, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{test(1), test(1)},
			{test(1), test(1)},
			{test(1)},
		}
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("evenly distributes shards based on run algorithm", func(t *testing.T) {
		input := []*Shard{
			{
				Name: "env1",
				Env:  env1,
				Tests: []Test{
					{
						Test:         makeTest(1, "fuchsia").Test,
						Runs:         5,
						RunAlgorithm: StopOnSuccess,
					},
					{
						Test:         makeTest(2, "fuchsia").Test,
						Runs:         5,
						RunAlgorithm: StopOnSuccess,
					},
				},
			}, {
				Name: "mult",
				Env:  env1,
				Tests: []Test{
					{
						Test:         makeTest(1, "fuchsia").Test,
						Runs:         5,
						RunAlgorithm: KeepGoing,
					},
				},
			},
		}
		actual := WithTargetDuration(input, 2, 0, 0, defaultDurations)
		expectedTests := [][]string{
			{test(1), test(2)},
			{test(1), test(1)},
			{test(1), test(1)},
			{test(1)},
		}
		expectedRuns := [][]runConfig{
			{{5, StopOnSuccess}, {5, StopOnSuccess}},
			{{2, KeepGoing}},
			{{2, KeepGoing}},
			{{1, KeepGoing}},
		}
		assertShardsContainRunConfigs(t, actual, expectedRuns)
		assertShardsContainTests(t, actual, expectedTests)
	})

	t.Run("sets a timeout for each shard", func(t *testing.T) {
		durations := TestDurationsMap{
			"*":     {MedianDuration: 1 * time.Minute},
			test(1): {MedianDuration: 5 * time.Minute},
		}
		actual := WithTargetDuration(defaultInput, 5, 0, 0, durations)
		expectedTests := [][]string{
			{test(1)},
			{test(2), test(3), test(4), test(5), test(6)},
		}
		assertShardsContainTests(t, actual, expectedTests)

		// Don't enforce an exact timeout to avoid being too brittle, just
		// ensure that the timeouts are within a reasonable range. The two
		// shards should have roughly similar timeouts because they have the
		// same expected duration (one runs 1 5-minute test, the other runs 5
		// 1-minute tests).
		minTimeout := 15 * time.Minute
		maxTimeout := 25 * time.Minute
		for _, shard := range actual {
			timeout := time.Duration(shard.TimeoutSecs) * time.Second
			if timeout < minTimeout {
				t.Errorf("%d-test shard has too short a timeout (%s < %s)", len(shard.Tests), timeout, minTimeout)
			} else if timeout > maxTimeout {
				t.Errorf("%d-test shard has too long a timeout (%s > %s)", len(shard.Tests), timeout, maxTimeout)
			}
		}
	})
}

func depsFile(t *testing.T, buildDir string, deps ...string) string {
	depsFile, err := os.CreateTemp(buildDir, "deps")
	if err != nil {
		t.Fatal(err)
	}
	name := depsFile.Name()
	depsFile.Close()
	b, err := json.Marshal([]string(deps))
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(name, b, 0o400); err != nil {
		t.Fatal(err)
	}
	return filepath.Base(name)
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
	buildDir := t.TempDir()
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
		tests := []Test{
			{
				Test: build.Test{
					Name:            "A",
					RuntimeDepsFile: depsFile(t, buildDir, "1", "2"),
				},
			}, {
				Test: build.Test{
					Name:            "B",
					RuntimeDepsFile: depsFile(t, buildDir, "3"),
				},
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
		tests := []Test{
			{
				Test: build.Test{
					Name:            "A",
					RuntimeDepsFile: depsFile(t, buildDir, "1", "2", "2"),
				},
			}, {
				Test: build.Test{
					Name:            "B",
					RuntimeDepsFile: depsFile(t, buildDir, "2", "3"),
				},
			},
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

func TestApplyRealmLabel(t *testing.T) {
	shardTests1 := []Test{
		{Test: build.Test{Name: "test1", OS: linux, CPU: "arm64"}},
		{Test: build.Test{Name: "test2", OS: linux, CPU: "arm64"}},
	}

	shardTests2 := []Test{
		{Test: build.Test{Name: "test3", OS: linux, CPU: x64}},
		{Test: build.Test{Name: "test4", OS: linux, CPU: x64}},
	}

	t.Run("realm label applies to all tests on all shards", func(t *testing.T) {
		shards := []*Shard{
			{Name: "foo", Tests: shardTests1},
			{Name: "bar", Tests: shardTests2},
		}

		ApplyRealmLabel(shards, "testrealm")

		expected := []*Shard{
			{
				Name: "foo",
				Tests: []Test{
					{
						Test:       build.Test{Name: "test1", OS: linux, CPU: "arm64"},
						RealmLabel: "testrealm",
					},
					{
						Test:       build.Test{Name: "test2", OS: linux, CPU: "arm64"},
						RealmLabel: "testrealm",
					},
				},
			},
			{
				Name: "bar",
				Tests: []Test{
					{
						Test:       build.Test{Name: "test3", OS: linux, CPU: x64},
						RealmLabel: "testrealm",
					},
					{
						Test:       build.Test{Name: "test4", OS: linux, CPU: x64},
						RealmLabel: "testrealm",
					},
				},
			},
		}

		assertEqual(t, expected, shards)
	})
}

func TestComputeShardTimeout(t *testing.T) {
	tests := []struct {
		name string
		// The predicted total expectedShardDuration of the shard.
		expectedShardDuration time.Duration
		// The timeout to set on each test.
		perTestTimeout time.Duration
		// The number of tests in the shard.
		testCount int
		// The expected timeout, as a duration string.
		want string
	}{
		{
			name:                  "average duration, average tests",
			expectedShardDuration: 10 * time.Minute,
			testCount:             250,
			want:                  "38m20s",
		},
		{
			name:                  "short duration, many tests",
			expectedShardDuration: time.Minute,
			testCount:             1000,
			want:                  "45m20s",
		},
		{
			name:                  "short duration, few tests",
			expectedShardDuration: 10 * time.Second,
			testCount:             3,
			want:                  "10m26s",
		},
		{
			name:                  "long duration, few tests",
			expectedShardDuration: time.Hour,
			testCount:             2,
			want:                  "2h10m4s",
		},
		{
			// Even if the expected shard duration is fairly short, we should
			// allocate enough time for tests to reach their timeouts.
			name:                  "tests with timeouts",
			expectedShardDuration: 15 * time.Minute,
			perTestTimeout:        30 * time.Minute,
			testCount:             10,
			want:                  "1h37m20s",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			s := subshard{duration: tc.expectedShardDuration}
			for i := 0; i < tc.testCount; i++ {
				s.tests = append(s.tests, Test{Timeout: tc.perTestTimeout})
			}
			if got := computeShardTimeout(s); got.String() != tc.want {
				t.Errorf("computeShardTimeout() = %s, want %s", got, tc.want)
			}
		})
	}
}

func TestApplyTestTimeouts(t *testing.T) {
	shards := []*Shard{
		{
			Name: "foo", Tests: []Test{
				{Test: build.Test{Name: "test1", OS: linux, CPU: "arm64"}},
				{Test: build.Test{Name: "test2", OS: linux, CPU: "arm64"}},
			},
		},
		{
			Name: "foo", Tests: []Test{
				{Test: build.Test{Name: "test3", OS: linux, CPU: x64}},
				{Test: build.Test{Name: "test4", OS: linux, CPU: x64}},
			},
		},
	}
	timeout := 37 * time.Minute
	ApplyTestTimeouts(shards, timeout)
	for _, shard := range shards {
		for _, test := range shard.Tests {
			if test.Timeout != timeout {
				t.Errorf("Test %s has wrong timeout %s, wanted %s", test.Name, test.Timeout, timeout)
			}
		}
	}
}
