// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"fmt"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Shard represents a set of tests with a common execution environment.
type Shard struct {
	// Name is the identifier for the shard.
	Name string `json:"name"`

	// Tests is the set of tests to be executed in this shard.
	Tests []build.Test `json:"tests"`

	// Env is a generalized notion of the execution environment for the shard.
	Env build.Environment `json:"environment"`
}

// MakeShards is the core algorithm to this tool. It takes a set of test specs and produces
// a set of shards which may then be converted into Swarming tasks.
// A single output Shard will contain only tests that have the same Envs.
//
// build.Environments that do not match all tags will be ignored.
//
// In Restricted mode, environments that don't specify a ServiceAccount will be ignored.
func MakeShards(specs []build.TestSpec, mode Mode, tags []string) []*Shard {
	// Collect the order of the shards so our shard ordering is deterministic with
	// respect to the input.
	envToSuites := newEnvMap()
	envs := []build.Environment{}
	for _, spec := range specs {
		for _, env := range spec.Envs {
			if !stringSlicesEq(tags, env.Tags) {
				continue
			}
			if mode == Restricted && env.ServiceAccount != "" {
				continue
			}

			// Tags should not differ by ordering.
			sortableTags := sort.StringSlice(tags)
			sortableTags.Sort()
			env.Tags = []string(sortableTags)

			specs, ok := envToSuites.get(env)
			if !ok {
				envs = append(envs, env)
			}
			envToSuites.set(env, append(specs, spec))
		}
	}
	shards := make([]*Shard, 0, len(envs))
	for _, env := range envs {
		specs, _ := envToSuites.get(env)
		sort.Slice(specs, func(i, j int) bool {
			return specs[i].Test.Name < specs[j].Test.Name
		})
		var tests []build.Test
		for _, spec := range specs {
			tests = append(tests, spec.Test)
		}
		shards = append(shards, &Shard{
			Name:  environmentName(env),
			Tests: tests,
			Env:   env,
		})
	}
	return shards
}

// MultiplyShards appends new shards to shards where each new shard contains one test
// repeated multiple times according to the specifications in multipliers.
func MultiplyShards(shards []*Shard, multipliers []TestModifier) []*Shard {
	for _, shard := range shards {
		for _, multiplier := range multipliers {
			for _, test := range shard.Tests {
				if multiplier.Name == test.Name && multiplier.OS == test.OS {
					shards = append(shards, &Shard{
						Name: "multiplied:" + shard.Name + "-" +
							normalizeTestName(test.Name),
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

// Abstracts a mapping build.Environment -> []string, as build.Environment contains non-comparable
// members (e.g., string slices), which makes it invalid for a map key.
type envMap struct {
	m map[string][]build.TestSpec
}

func newEnvMap() envMap {
	return envMap{m: make(map[string][]build.TestSpec)}
}

func (em envMap) get(e build.Environment) ([]build.TestSpec, bool) {
	specs, ok := em.m[fmt.Sprintf("%v", e)]
	return specs, ok
}

func (em *envMap) set(e build.Environment, specs []build.TestSpec) {
	em.m[fmt.Sprintf("%v", e)] = specs
}

func stringSlicesEq(s []string, t []string) bool {
	if len(s) != len(t) {
		return false
	}
	seen := make(map[string]int)
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
