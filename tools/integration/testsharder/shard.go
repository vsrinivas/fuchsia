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

	// Deps is the list of runtime dependencies required to be present on the host
	// at shard execution time. It is a list of paths relative to the fuchsia
	// build directory.
	Deps []string `json:"deps,omitempty"`
}

// ShardOptions parametrize sharding behavior.
type ShardOptions struct {
	// Mode is a general mode in which the testsharder will be run. See mode.go
	// for more details.
	Mode Mode

	// Tags is the list of tags that the sharded Environments must match; those
	// that don't match all tags will be ignored.
	Tags []string
}

// MakeShards returns the list of shards associated with a given build.
// A single output shard will contain only tests that have the same environment.
func MakeShards(specs []build.TestSpec, opts *ShardOptions) []*Shard {
	// Collect the order of the shards so our shard ordering is deterministic with
	// respect to the input.
	envToSuites := newEnvMap()
	envs := []build.Environment{}
	for _, spec := range specs {
		for _, env := range spec.Envs {
			if !stringSlicesEq(opts.Tags, env.Tags) {
				continue
			}
			if opts.Mode == Restricted && env.ServiceAccount != "" {
				continue
			}

			// Tags should not differ by ordering.
			sortableTags := sort.StringSlice(opts.Tags)
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

// EnvironmentName returns a name for an environment.
func environmentName(env build.Environment) string {
	tokens := []string{}
	addToken := func(s string) {
		if s != "" {
			// s/-/_, so there is no ambiguity among the tokens
			// making up a name.
			s = strings.Replace(s, "-", "_", -1)
			tokens = append(tokens, s)
		}
	}

	addToken(env.Dimensions.DeviceType)
	addToken(env.Dimensions.OS)
	addToken(env.Dimensions.Testbed)
	addToken(env.Dimensions.Pool)
	if env.ServiceAccount != "" {
		addToken(strings.Split(env.ServiceAccount, "@")[0])
	}
	if env.Netboot {
		addToken("netboot")
	}
	return strings.Join(tokens, "-")
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
