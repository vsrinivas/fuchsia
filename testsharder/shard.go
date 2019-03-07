// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"sort"
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
// a set of shards which may then be converted into Swarming tasks. Environments with a
// label different from the provided will be ignored.
//
// This is the most naive algorithm at the moment. It just merges all tests together which
// have the same environment setting into the same shard.
func MakeShards(specs []TestSpec, label string) []*Shard {
	// Collect the order of the shards so our shard ordering is deterministic with
	// respect to the input.
	envToSuites := make(map[Environment][]TestSpec)
	envs := []Environment{}
	for _, spec := range specs {
		for _, env := range spec.Envs {
			if env.Label != label {
				continue
			}
			if _, ok := envToSuites[env]; !ok {
				envs = append(envs, env)
			}
			envToSuites[env] = append(envToSuites[env], spec)
		}
	}
	shards := make([]*Shard, 0, len(envs))
	for _, env := range envs {
		specs := envToSuites[env]
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
