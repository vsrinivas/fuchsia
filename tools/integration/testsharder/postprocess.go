// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"container/heap"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

const maxShardsPerEnvironment = 8

func ExtractDeps(shards []*Shard, fuchsiaBuildDir string) error {
	for _, shard := range shards {
		if err := extractDepsFromShard(shard, fuchsiaBuildDir); err != nil {
			return err
		}
	}
	return nil
}

func extractDepsFromShard(shard *Shard, fuchsiaBuildDir string) error {
	var shardDeps []string
	for i := range shard.Tests {
		deps, err := extractDepsFromTest(&shard.Tests[i], fuchsiaBuildDir)
		if err != nil {
			return err
		}
		shardDeps = append(shardDeps, deps...)
	}
	shardDeps = dedupe(shardDeps)
	sort.Strings(shardDeps)
	shard.Deps = shardDeps
	return nil
}

func extractDepsFromTest(test *build.Test, fuchsiaBuildDir string) ([]string, error) {
	if test.RuntimeDepsFile == "" {
		return nil, nil
	}
	path := filepath.Join(fuchsiaBuildDir, test.RuntimeDepsFile)
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	// RuntimeDepsFile is no longer needed at this point and clutters the output.
	test.RuntimeDepsFile = ""
	var deps []string
	err = json.NewDecoder(f).Decode(&deps)
	return deps, err
}

func dedupe(l []string) []string {
	var deduped []string
	m := make(map[string]bool)
	for _, s := range l {
		m[s] = true
	}
	for s := range m {
		deduped = append(deduped, s)
	}
	return deduped
}

// MultiplyShards appends new shards to shards where each new shard contains one test
// repeated multiple times according to the specifications in multipliers.
func MultiplyShards(shards []*Shard, multipliers []TestModifier) []*Shard {
	for _, shard := range shards {
		for _, multiplier := range multipliers {
			for _, test := range shard.Tests {
				if multiplier.Name == test.Name && multiplier.OS == test.OS {
					shards = append(shards, &Shard{
						Name:  "multiplied:" + shard.Name + "-" + normalizeTestName(test.Name),
						Tests: multiplyTest(test, multiplier.TotalRuns),
						Env:   shard.Env,
					})
				}
			}
		}
	}
	return shards
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func divRoundUp(a, b int) int {
	if a%b == 0 {
		return a / b
	}
	return (a / b) + 1
}

// WithSize returns a list of shards such that each shard contains "roughly"
// targetSize tests.
// If targetSize <= 0, just returns its input.
func WithSize(shards []*Shard, targetSize int, testDurations TestDurationsMap) []*Shard {
	if targetSize <= 0 {
		return shards
	}
	output := make([]*Shard, 0, len(shards))
	for _, shard := range shards {
		numNewShards := min(
			divRoundUp(len(shard.Tests), targetSize),
			maxShardsPerEnvironment,
		)
		newShards := shardByTime(shard, testDurations, numNewShards)
		output = append(output, newShards...)
	}
	return output
}

type subshard struct {
	duration time.Duration
	tests    []build.Test
}

// A subshardHeap is a min heap of subshards, using subshard duration as the key
// to sort by.
// It implements heap.Interface.
type subshardHeap []subshard

func (h subshardHeap) Len() int {
	return len(h)
}

func (h subshardHeap) Less(i, j int) bool {
	return h[i].duration < h[j].duration
}

func (h subshardHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
}

func (h *subshardHeap) Push(s interface{}) {
	*h = append(*h, s.(subshard))
}

func (h *subshardHeap) Pop() interface{} {
	old := *h
	n := len(old)
	s := old[n-1]
	*h = old[0 : n-1]
	return s
}

// shardByTime breaks a sigle original shard into numNewShards subshards such
// that each subshard has approximately the same expected total duration.
//
// It does this using a greedy approximation algorithm for static multiprocessor
// scheduling (https://en.wikipedia.org/wiki/Multiprocessor_scheduling). It
// first sorts the tests in descending order by expected duration and then
// successively allocates each test to the subshard with the lowest total
// expected duration so far.
func shardByTime(shard *Shard, testDurations TestDurationsMap, numNewShards int) []*Shard {
	sort.Slice(shard.Tests, func(index1, index2 int) bool {
		test1, test2 := shard.Tests[index1], shard.Tests[index2]
		duration1 := testDurations.Get(test1).MedianDuration
		duration2 := testDurations.Get(test2).MedianDuration
		if duration1 == duration2 {
			// Sort by name for tests of equal duration to ensure deterministic
			// ordering.
			return test1.Name < test2.Name
		}
		// "greater than" instead of "less than" to achieve descending ordering
		return duration1 > duration2
	})

	h := (subshardHeap)(make([]subshard, numNewShards))

	for _, test := range shard.Tests {
		// Assign this test to the subshard with the lowest total expected
		// duration at this iteration of the for loop.
		ss := heap.Pop(&h).(subshard)
		ss.duration += testDurations.Get(test).MedianDuration
		ss.tests = append(ss.tests, test)
		heap.Push(&h, ss)
	}

	// Sort the resulting shards by the basename of the first test. Otherwise,
	// changes to the input set of tests (adding, removing or renaming a test)
	// result in confusing reordering of the shard names. This ensures that a
	// given named shard (e.g. "QEMU-(1)") will generally contain the same set
	// of tests across multiple builds, even if the input set of tests changes.
	sort.Slice(h, func(i, j int) bool {
		return h[i].tests[0].Name < h[j].tests[0].Name
	})

	newShards := make([]*Shard, 0, numNewShards)
	for i, subshard := range h {
		name := shard.Name
		if numNewShards > 1 {
			name = fmt.Sprintf("%s-(%d)", shard.Name, i+1)
		}
		newShards = append(newShards, &Shard{
			Name:  name,
			Tests: subshard.tests,
			Env:   shard.Env,
		})
	}
	return newShards
}

// Removes leading slashes and replaces all other `/` with `_`. This allows the
// shard name to appear in filepaths.
func normalizeTestName(name string) string {
	trimmedName := strings.TrimLeft(name, "/")
	return strings.ReplaceAll(trimmedName, "/", "_")
}

// Returns a list of Tests containing the same test multiplied by the number of runs.
func multiplyTest(test build.Test, runs int) []build.Test {
	var tests []build.Test
	for i := 1; i <= runs; i++ {
		testCopy := test
		testCopy.Name = fmt.Sprintf("%s (%d)", test.Name, i)
		tests = append(tests, testCopy)
	}
	return tests
}
