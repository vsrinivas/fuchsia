// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"fmt"
	"sort"
	"strings"
)

// Shard represents a set of tests with a common execution environment.
type Shard struct {
	// Name is the identifier for the shard.
	Name string `json:"name"`

	// Tests is the set of test to be executed in this shard.
	Tests []Test `json:"tests"`

	// Env is a generalized notion of the execution environment for the shard.
	Env Environment `json:"environment"`
}

// MakeShards is the core algorithm to this tool. It takes a set of test specs and produces
// a set of shards which may then be converted into Swarming tasks.
//
// Environments that do not match all specified tags will be ignored.
//
// This is the most naive algorithm at the moment. It just merges all tests together which
// have the same environment setting into the same shard.
func MakeShards(specs []TestSpec, mode Mode, tags []string) []*Shard {
	// Collect the order of the shards so our shard ordering is deterministic with
	// respect to the input.
	envToSuites := newEnvMap()
	envs := []Environment{}
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
			return specs[i].Test.Path < specs[i].Test.Path
		})
		var tests []Test
		for _, spec := range specs {
			tests = append(tests, spec.Test)
		}
		shards = append(shards, &Shard{
			Name:  env.Name(),
			Tests: tests,
			Env:   env,
		})
	}
	return shards
}

// Appends new shards to shards where each new shard contains one test repeated
// multiple times according to the specifications in multipliers.
func MultiplyShards(shards []*Shard, multipliers []TestModifier) ([]*Shard, error) {
	multipliersFound := make(map[TestModifier]bool)
	for _, shard := range shards {
		for _, multiplier := range multipliers {
			for _, test := range shard.Tests {
				if multiplier.Target == test.Name && multiplier.OS == test.OS {
					shards = append(shards, &Shard{
						Name:  shard.Name + "-" + normalizeTestName(test.Name),
						Tests: multiplyTest(test, multiplier.TotalRuns),
						Env:   shard.Env,
					})
					multipliersFound[multiplier] = true
				}
			}
		}
	}
	if len(multipliersFound) != len(multipliers) {
		return nil, fmt.Errorf("Not all of the multiplier targets were found in the test manifest. Make sure the targets appear in $root_build_dir/tests.json")
	}
	return shards, nil
}

// Removes leading slashes and replaces all other `/` with `_`. This allows the
// shard name to appear in filepaths.
func normalizeTestName(name string) string {
	trimmedName := strings.TrimLeft(name, "/")
	return strings.ReplaceAll(trimmedName, "/", "_")
}

// Returns a list of Tests containing the same test multiplied by the number of runs.
func multiplyTest(test Test, runs int) []Test {
	var tests []Test
	for i := 1; i <= runs; i++ {
		testCopy := test
		testCopy.Name = fmt.Sprintf("%s (%d)", test.Name, i)
		tests = append(tests, testCopy)
	}
	return tests
}

// Abstracts a mapping Environment -> []string, as Environment contains non-comparable
// members (e.g., string slices), which makes it invalid for a map key.
type envMap struct {
	m map[string][]TestSpec
}

func newEnvMap() envMap {
	return envMap{m: make(map[string][]TestSpec)}
}

func (em envMap) get(e Environment) ([]TestSpec, bool) {
	specs, ok := em.m[fmt.Sprintf("%v", e)]
	return specs, ok
}

func (em *envMap) set(e Environment, specs []TestSpec) {
	em.m[fmt.Sprintf("%v", e)] = specs
}

func stringSlicesEq(s []string, t []string) bool {
	if len(s) != len(t) {
		return false
	}
	seen := make(map[string]int)
	for i, _ := range s {
		seen[s[i]] += 1
		seen[t[i]] -= 1
	}
	for _, v := range seen {
		if v != 0 {
			return false
		}
	}
	return true
}
