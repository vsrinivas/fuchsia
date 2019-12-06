// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

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

// WithMaxSize returns a list of shards such that each shard contains fewer than maxShardSize tests.
// If maxShardSize <= 0, just returns its input.
func WithMaxSize(shards []*Shard, maxShardSize int) []*Shard {
	if maxShardSize <= 0 {
		return shards
	}
	output := make([]*Shard, 0, len(shards))
	for _, shard := range shards {
		numNewShards := divRoundUp(len(shard.Tests), maxShardSize)
		// Evenly distribute the tests between the new shards.
		maxTestsPerNewShard := divRoundUp(len(shard.Tests), numNewShards)
		for i := 0; i < numNewShards; i++ {
			sliceStart := i * maxTestsPerNewShard
			sliceLimit := min((i+1)*maxTestsPerNewShard, len(shard.Tests))
			newName := shard.Name
			if numNewShards > 1 {
				newName = fmt.Sprintf("%s-(%d)", shard.Name, i+1)
			}
			output = append(output, &Shard{
				Name:  newName,
				Tests: shard.Tests[sliceStart:sliceLimit],
				Env:   shard.Env,
			})
		}
	}
	return output
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
