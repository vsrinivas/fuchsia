// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"fmt"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Note that just printing a list of shard pointers will print a list of memory addresses,
// which would make for an unhelpful error message.
func assertEqual(t *testing.T, expected, actual []*Shard) {
	if !reflect.DeepEqual(expected, actual) {
		errMsg := "\nexpected:\n"
		for _, shard := range expected {
			errMsg += fmt.Sprintf("%v,\n", shard)
		}
		errMsg += "\nactual:\n"
		for _, shard := range actual {
			errMsg += fmt.Sprintf("%v,\n", shard)
		}
		t.Fatalf(errMsg)
	}
}

func spec(id int, envs ...build.Environment) build.TestSpec {
	return build.TestSpec{
		Test: build.Test{
			PackageURL: fmt.Sprintf("test%d", id),
			Path:       fmt.Sprintf("/path/to/test/%d", id),
			OS:         "fuchsia",
		},
		Envs: envs,
	}
}

func shard(env build.Environment, ids ...int) *Shard {
	return namedShard(env, environmentName(env), ids...)
}

func namedShard(env build.Environment, name string, ids ...int) *Shard {
	var tests []build.Test
	for _, id := range ids {
		tests = append(tests, spec(id, env).Test)
	}
	return &Shard{
		Name:  name,
		Tests: tests,
		Env:   env,
	}
}

func TestMakeShards(t *testing.T) {
	env1 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := build.Environment{
		Dimensions: build.DimensionSet{DeviceType: "NUC"},
		Tags:       []string{},
	}
	env3 := build.Environment{
		Dimensions: build.DimensionSet{OS: "Linux"},
		Tags:       []string{},
	}

	basicOpts := &ShardOptions{
		Mode: Normal,
		Tags: []string{},
	}

	t.Run("environments have nonempty names", func(t *testing.T) {
		envs := []build.Environment{env1, env2, env3}
		for _, env := range envs {
			if environmentName(env) == "" {
				t.Fatalf("build.Environment\n%+v\n has an empty name", env)
			}
		}
	})

	t.Run("tests of same environment are grouped", func(t *testing.T) {
		actual := MakeShards(
			[]build.TestSpec{spec(1, env1, env2), spec(2, env1, env3), spec(3, env3)},
			basicOpts,
		)
		expected := []*Shard{shard(env1, 1, 2), shard(env2, 1), shard(env3, 2, 3)}
		assertEqual(t, expected, actual)
	})

	t.Run("there is no deduplication of tests", func(t *testing.T) {
		actual := MakeShards(
			[]build.TestSpec{spec(1, env1), spec(1, env1), spec(1, env1)},
			basicOpts,
		)
		expected := []*Shard{shard(env1, 1, 1, 1)}
		assertEqual(t, expected, actual)
	})

	// Ensure that the order of the shards is the order in which their
	// corresponding environments appear in the input. This is the simplest
	// deterministic order we can produce for the shards.
	t.Run("shards are ordered", func(t *testing.T) {
		actual := MakeShards(
			[]build.TestSpec{spec(1, env2, env3), spec(2, env1), spec(3, env3)},
			basicOpts,
		)
		expected := []*Shard{shard(env2, 1), shard(env3, 1, 3), shard(env1, 2)}
		assertEqual(t, expected, actual)
	})

	t.Run("tags are respected", func(t *testing.T) {
		tagger := func(env build.Environment, tags ...string) build.Environment {
			env2 := env
			env2.Tags = tags
			return env2
		}

		actual := MakeShards(
			[]build.TestSpec{
				spec(1, tagger(env1, "A")),
				spec(2, tagger(env1, "A", "B", "C")),
				spec(3, tagger(env2, "B", "C")),
				spec(4, tagger(env3, "C", "A")),
				spec(5, tagger(env3, "A", "C")),
			},
			&ShardOptions{
				Mode: Normal,
				Tags: []string{"A", "C"},
			},
		)
		expected := []*Shard{
			// "C", "A" and "A", "C" should define the same tags.
			shard(tagger(env3, "A", "C"), 4, 5),
		}
		assertEqual(t, expected, actual)
	})

	t.Run("different service accounts get different shards", func(t *testing.T) {
		withAcct := func(env build.Environment, acct string) build.Environment {
			env2 := env
			env2.ServiceAccount = acct
			return env2
		}

		actual := MakeShards(
			[]build.TestSpec{
				spec(1, env1),
				spec(1, withAcct(env1, "acct1")),
				spec(1, withAcct(env1, "acct2")),
			},
			basicOpts,
		)
		expected := []*Shard{
			shard(env1, 1),
			shard(withAcct(env1, "acct1"), 1),
			shard(withAcct(env1, "acct2"), 1),
		}
		assertEqual(t, expected, actual)
	})

	t.Run("restricted mode is respected", func(t *testing.T) {
		withAcct := func(env build.Environment, acct string) build.Environment {
			env2 := env
			env2.ServiceAccount = acct
			return env2
		}

		actual := MakeShards(
			[]build.TestSpec{
				spec(1, env1),
				spec(2, withAcct(env1, "acct1")),
				spec(3, withAcct(env1, "acct2")),
			},
			&ShardOptions{
				Mode: Restricted,
				Tags: []string{},
			},
		)
		expected := []*Shard{
			shard(env1, 1),
		}
		assertEqual(t, expected, actual)
	})
	t.Run("netboot envs get different shards", func(t *testing.T) {
		withNetboot := func(env build.Environment) build.Environment {
			env2 := env
			env2.Netboot = true
			return env2
		}

		actual := MakeShards(
			[]build.TestSpec{
				spec(1, env1),
				spec(1, withNetboot(env1)),
			},
			basicOpts,
		)
		expected := []*Shard{
			shard(env1, 1),
			shard(withNetboot(env1), 1),
		}
		assertEqual(t, expected, actual)
	})
}
