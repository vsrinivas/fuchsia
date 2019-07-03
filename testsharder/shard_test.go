// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"fmt"
	"reflect"
	"testing"
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

func spec(id int, envs ...Environment) TestSpec {
	return TestSpec{
		Test: Test{
			Location: fmt.Sprintf("/path/to/test/%d", id),
		},
		Envs: envs,
	}
}

func shard(env Environment, ids ...int) *Shard {
	return namedShard(env, env.Name(), ids...)
}

func namedShard(env Environment, name string, ids ...int) *Shard {
	var tests []Test
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
	env1 := Environment{
		Dimensions: DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := Environment{
		Dimensions: DimensionSet{DeviceType: "NUC"},
		Tags:       []string{},
	}
	env3 := Environment{
		Dimensions: DimensionSet{OS: "Linux"},
		Tags:       []string{},
	}
	t.Run("environments have nonempty names", func(t *testing.T) {
		envs := []Environment{env1, env2, env3}
		for _, env := range envs {
			if env.Name() == "" {
				t.Fatalf("Environment\n%+v\n has an empty name", env)
			}
		}
	})

	t.Run("tests of same environment are grouped", func(t *testing.T) {
		actual := MakeShards(
			[]TestSpec{spec(1, env1, env2), spec(2, env1, env3), spec(3, env3)},
			Normal,
			[]string{},
		)
		expected := []*Shard{shard(env1, 1, 2), shard(env2, 1), shard(env3, 2, 3)}
		assertEqual(t, expected, actual)
	})

	t.Run("there is no deduplication of tests", func(t *testing.T) {
		actual := MakeShards(
			[]TestSpec{spec(1, env1), spec(1, env1), spec(1, env1)},
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
			[]TestSpec{spec(1, env2, env3), spec(2, env1), spec(3, env3)},
			Normal,
			[]string{},
		)
		expected := []*Shard{shard(env2, 1), shard(env3, 1, 3), shard(env1, 2)}
		assertEqual(t, expected, actual)
	})

	t.Run("tags are respected", func(t *testing.T) {
		tagger := func(env Environment, tags ...string) Environment {
			env2 := env
			env2.Tags = tags
			return env2
		}

		actual := MakeShards(
			[]TestSpec{
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
		withAcct := func(env Environment, acct string) Environment {
			env2 := env
			env2.ServiceAccount = acct
			return env2
		}

		actual := MakeShards(
			[]TestSpec{
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
		withAcct := func(env Environment, acct string) Environment {
			env2 := env
			env2.ServiceAccount = acct
			return env2
		}

		actual := MakeShards(
			[]TestSpec{
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
}

func TestMultiplyShards(t *testing.T) {
	env1 := Environment{
		Dimensions: DimensionSet{DeviceType: "QEMU"},
		Tags:       []string{},
	}
	env2 := Environment{
		Dimensions: DimensionSet{DeviceType: "NUC"},
		Tags:       []string{},
	}
	env3 := Environment{
		Dimensions: DimensionSet{OS: "Linux"},
		Tags:       []string{},
	}
	makeTest := func(id int, os OS) Test {
		return Test{
			Name:     fmt.Sprintf("test%d", id),
			Location: fmt.Sprintf("/path/to/test/%d", id),
			OS:       os,
		}
	}

	shard := func(env Environment, os OS, ids ...int) *Shard {
		var tests []Test
		for _, id := range ids {
			tests = append(tests, makeTest(id, os))
		}
		return &Shard{
			Name:  env.Name(),
			Tests: tests,
			Env:   env,
		}
	}

	makeTestModifier := func(id int, os OS, runs int) TestModifier {
		return TestModifier{
			Target:    fmt.Sprintf("test%d", id),
			OS:        os,
			TotalRuns: runs,
		}
	}

	multShard := func(env Environment, os OS, id int, runs int) *Shard {
		var tests []Test
		test := makeTest(id, os)
		for i := 1; i <= runs; i++ {
			testCopy := test
			testCopy.Name = fmt.Sprintf("%s (%d)", test.Name, i)
			tests = append(tests, testCopy)
		}
		return &Shard{
			Name:  env.Name() + "-" + test.Name,
			Tests: tests,
			Env:   env,
		}
	}

	t.Run("multiply tests in shards", func(t *testing.T) {
		shards := []*Shard{
			shard(env1, Fuchsia, 1),
			shard(env2, Fuchsia, 1, 2),
			shard(env3, Linux, 3),
		}
		multipliers := []TestModifier{
			makeTestModifier(1, Fuchsia, 2),
			makeTestModifier(3, Linux, 3),
		}
		actual, err := MultiplyShards(
			shards,
			multipliers,
		)
		if err != nil {
			t.Fatalf("failed with errmsg: %v", err)
		}
		expected := append(
			shards,
			multShard(env1, Fuchsia, 1, 2),
			multShard(env2, Fuchsia, 1, 2),
			multShard(env3, Linux, 3, 3),
		)
		assertEqual(t, expected, actual)
	})

	t.Run("fail to multiply shards with invalid multipliers", func(t *testing.T) {
		shards := []*Shard{
			shard(env1, Fuchsia, 1),
			shard(env2, Fuchsia, 1, 2),
			shard(env3, Linux, 3),
		}
		multipliers := []TestModifier{
			makeTestModifier(1, Linux, 2),
			makeTestModifier(3, Linux, 3),
		}
		_, err := MultiplyShards(
			shards,
			multipliers,
		)
		if err == nil {
			t.Fatalf("did not fail for invalid multipliers")
		}
	})
}

func TestWithMaxSize(t *testing.T) {
	env1 := Environment{
		Tags: []string{"env1"},
	}
	env2 := Environment{
		Dimensions: DimensionSet{DeviceType: "env2"},
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
	t.Run("max is larger than all shards", func(t *testing.T) {
		maxSize := len(input[0].Tests)+len(input[1].Tests)
		actual := WithMaxSize(input, maxSize)
		assertEqual(t, []*Shard{
			// Returns equivalent shards, but renamed.
			namedShard(env1, "env1-(0)", 1, 2, 3, 4, 5), namedShard(env2, "env2-(0)", 6, 7, 8)},
			actual)
			assertShardsLessThanSize(t, actual, maxSize)
	})
	t.Run("applies max", func(t *testing.T) {
		maxSize := 2
		actual := WithMaxSize(input, maxSize)
		assertEqual(t, []*Shard{
			namedShard(env1, "env1-(0)", 1, 2), namedShard(env1, "env1-(1)", 3, 4),
			namedShard(env1, "env1-(2)", 5),
			namedShard(env2, "env2-(0)", 6, 7), namedShard(env2, "env2-(1)", 8)},
			actual)
		assertShardsLessThanSize(t, actual, maxSize)
	})
}
