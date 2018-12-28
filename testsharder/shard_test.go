// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder_test

import (
	"fmt"
	"reflect"
	"testing"

	"fuchsia.googlesource.com/tools/testsharder"
)

// Note that just printing a list of shard pointers will print a list of memory addresses,
// which would make for an unhelpful error message.
func assertEqual(t *testing.T, expected, actual []*testsharder.Shard) {
	if !reflect.DeepEqual(expected, actual) {
		errMsg := "expected:\n"
		for _, shard := range expected {
			errMsg += fmt.Sprintf("%+v,\n", shard)
		}
		errMsg += "actual:\n"
		for _, shard := range actual {
			errMsg += fmt.Sprintf("%+v,\n", shard)
		}
		t.Fatalf(errMsg)
	}
}

func TestMakeShards(t *testing.T) {
	test1 := testsharder.Test{Location: "/path/to/binary"}
	test2 := testsharder.Test{Location: "/path/to/binary2"}
	hostDeps1 := []string{"host/side/dep/1a", "host/side/dep/1b"}
	hostDeps2 := []string{"host/side/dep/2a", "host/side/dep/2b"}
	env1 := testsharder.Environment{
		Dimensions: testsharder.DimensionSet{DeviceType: "QEMU"},
	}
	env2 := testsharder.Environment{
		Dimensions: testsharder.DimensionSet{DeviceType: "NUC"},
	}
	env3 := testsharder.Environment{
		Dimensions: testsharder.DimensionSet{DeviceType: "NUC"},
		Label:      "LABEL",
	}
	spec1 := testsharder.TestSpec{
		Test: test1,
		Envs: []testsharder.Environment{env1},
	}
	spec2 := testsharder.TestSpec{
		Test: test1,
		Envs: []testsharder.Environment{env1},
	}
	spec3 := testsharder.TestSpec{
		Test: test2,
		Envs: []testsharder.Environment{env1, env2},
	}
	spec4 := testsharder.TestSpec{
		Test: test2,
		Envs: []testsharder.Environment{env1, env2, env3},
	}
	t.Run("Ensure that environments have names", func(t *testing.T) {
		// This will in turn ensure that the associated shards too have
		// names.
		if env1.Name() == "" {
			t.Fatalf("Environment\n%+v\n has an empty name", env1)
		}
		if env2.Name() == "" {
			t.Fatalf("Environment\n%+v\n has an empty name", env2)
		}
	})

	t.Run("Ensure env shared", func(t *testing.T) {
		actual := testsharder.MakeShards([]testsharder.TestSpec{spec1, spec2, spec3}, "")
		expected := []*testsharder.Shard{
			// Ensure that the order of the shards is the order in which their
			// corresponding environments appear in the input. This is the simplest
			// deterministic order we can produce for the shards.
			{
				Name: env1.Name(),
				// Ensure that we actually specify the test _twice_, that is, don't
				// necessarily deduplicate tests for a shared environment.
				Tests: []testsharder.Test{test1, test1, test2},
				Env:   env1,
			},
			{
				Name:  env2.Name(),
				Tests: []testsharder.Test{test2},
				Env:   env2,
			},
		}
		assertEqual(t, expected, actual)
	})

	t.Run("Ensure label respected", func(t *testing.T) {
		actual := testsharder.MakeShards([]testsharder.TestSpec{spec1, spec2, spec3, spec4}, env3.Label)
		expected := []*testsharder.Shard{
			{
				Name:  env3.Name(),
				Tests: []testsharder.Test{test2},
				Env:   env3,
			},
		}
		assertEqual(t, expected, actual)
	})

	t.Run("Ensure host deps are aggregated", func(t *testing.T) {
		linuxEnv := testsharder.Environment{
			Dimensions: testsharder.DimensionSet{OS: "Linux"},
		}
		macEnv := testsharder.Environment{
			Dimensions: testsharder.DimensionSet{OS: "Mac"},
		}
		actual := testsharder.MakeShards([]testsharder.TestSpec{
			{
				Test:     test1,
				Envs:     []testsharder.Environment{linuxEnv},
				HostDeps: hostDeps1,
			},
			{
				Test:     test2,
				Envs:     []testsharder.Environment{linuxEnv},
				HostDeps: hostDeps2,
			},
			{
				Test:     test1,
				Envs:     []testsharder.Environment{macEnv},
				HostDeps: hostDeps1,
			},
		}, "")
		expected := []*testsharder.Shard{
			{
				Name:  linuxEnv.Name(),
				Tests: []testsharder.Test{test1, test2},
				Env:   linuxEnv,
				HostDeps: map[string][]string{
					test1.Location: hostDeps1,
					test2.Location: hostDeps2,
				},
			},
			{
				Name:  macEnv.Name(),
				Tests: []testsharder.Test{test1},
				Env:   macEnv,
				HostDeps: map[string][]string{
					test1.Location: hostDeps1,
				},
			},
		}
		assertEqual(t, expected, actual)
	})
}
