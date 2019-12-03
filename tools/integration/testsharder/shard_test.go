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
			Path: fmt.Sprintf("/path/to/test/%d", id),
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
			Normal,
			[]string{},
		)
		expected := []*Shard{shard(env1, 1, 2), shard(env2, 1), shard(env3, 2, 3)}
		assertEqual(t, expected, actual)
	})

	t.Run("there is no deduplication of tests", func(t *testing.T) {
		actual := MakeShards(
			[]build.TestSpec{spec(1, env1), spec(1, env1), spec(1, env1)},
			Normal,
			[]string{},
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
			Normal,
			[]string{},
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
			Normal,
			[]string{"A", "C"},
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
			Normal,
			[]string{},
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
			Restricted,
			[]string{},
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
			Normal,
			[]string{},
		)
		expected := []*Shard{
			shard(env1, 1),
			shard(withNetboot(env1), 1),
		}
		assertEqual(t, expected, actual)
	})
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
		Dimensions: build.DimensionSet{OS: "Linux"},
		Tags:       []string{},
	}
	makeTest := func(id int, os string) build.Test {
		return build.Test{
			Name:  fmt.Sprintf("test%d", id),
			Label: fmt.Sprintf("//path/to/target:test%d(//toolchain)", id),
			Path:  fmt.Sprintf("/path/to/test/%d", id),
			OS:    os,
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
			shard(env2, "fuchsia", 1, 2),
			shard(env3, "linux", 3),
		}
		multipliers := []TestModifier{
			makeTestModifier(1, "fuchsia", 2),
			makeTestModifier(3, "linux", 3),
		}
		actual := MultiplyShards(
			shards,
			multipliers,
		)
		expected := append(
			shards,
			multShard(env1, "fuchsia", 1, 2),
			multShard(env2, "fuchsia", 1, 2),
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
