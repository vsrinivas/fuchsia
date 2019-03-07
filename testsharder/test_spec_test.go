// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"testing"

	"fuchsia.googlesource.com/tools/build"
)

var qemuPlatform = DimensionSet{
	DeviceType: "QEMU",
}

var nucPlatform = DimensionSet{
	DeviceType: "NUC",
}

var linuxPlatform = DimensionSet{
	OS: "Linux",
}

var macPlatform = DimensionSet{
	OS: "Mac",
}

var qemuEnv = Environment{
	Dimensions: qemuPlatform,
}

var nucEnv = Environment{
	Dimensions: nucPlatform,
}

var linuxEnv = Environment{
	Dimensions: linuxPlatform,
}

var macEnv = Environment{
	Dimensions: macPlatform,
}

var specFoo1 = TestSpec{
	Test: Test{
		Name:    "//obsidian/bin/foo:foo_unittests",
		Path:    "/system/test/foo_unittests",
		OS:      Fuchsia,
		Command: []string{"/system/test/foo_unittests", "bar", "baz"},
	},
	Envs: []Environment{qemuEnv},
}

var specFoo2 = TestSpec{
	Test: Test{
		Name: "//obsidian/bin/foo:foo_integration_tests",
		Path: "/system/test/foo_integration_tests",
		OS:   Fuchsia,
	},
	Envs: []Environment{qemuEnv, nucEnv},
}

var specBar = TestSpec{
	Test: Test{
		Name: "//obsidian/lib/bar:bar_tests",
		Path: "/system/test/bar_tests",
		OS:   Fuchsia,
	},
	Envs: []Environment{qemuEnv},
}

var specBaz = TestSpec{
	Test: Test{
		Name: "//obsidian/public/lib/baz:baz_host_tests",
		Path: "/$root_build_dir/baz_host_tests",
		OS:   Linux,
	},
	Envs: []Environment{linuxEnv, macEnv},
}

func TestLoadTestSpecs(t *testing.T) {
	areEqual := func(a, b []TestSpec) bool {
		stringify := func(spec TestSpec) string {
			return fmt.Sprintf("%#v", spec)
		}
		sort := func(list []TestSpec) {
			sort.Slice(list[:], func(i, j int) bool {
				return stringify(list[i]) < stringify(list[j])
			})
		}
		sort(a)
		sort(b)
		return reflect.DeepEqual(a, b)
	}

	tmpDir, err := ioutil.TempDir("", "test-spec")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	deps := []string{"path/to/first/dep", "path/to/second"}
	depsFilepath := filepath.Join(tmpDir, "deps.json")
	df, err := os.Create(depsFilepath)
	if err != nil {
		t.Fatal(err)
	}
	defer df.Close()
	if err := json.NewEncoder(df).Encode(&deps); err != nil {
		t.Fatalf("failed to create JSON encoder: %v", err)
	}

	specBazIn := specBaz
	specBazIn.DepsFile = "deps.json"
	initial := []TestSpec{specBar, specBazIn}

	manifest := filepath.Join(tmpDir, build.TestSpecManifestName)
	m, err := os.Create(manifest)
	if err != nil {
		t.Fatal(err)
	}
	defer m.Close()
	if err := json.NewEncoder(m).Encode(&initial); err != nil {
		t.Fatal(err)
	}

	actual, err := LoadTestSpecs(tmpDir)
	if err != nil {
		t.Fatalf("failed to load test specs: %v", err)
	}

	specBazOut := specBaz
	specBazOut.Deps = deps
	expected := []TestSpec{specBar, specBazOut}

	if !areEqual(expected, actual) {
		t.Fatalf("test specs not properly loaded:\nexpected:\n%+v\nactual:\n%+v", expected, actual)
	}
}

func TestValidateTestSpecs(t *testing.T) {
	noTestNameSpec := TestSpec{
		Test: Test{
			Path: "/system/test/baz_tests",
			OS:   Linux,
		},
		Envs: []Environment{qemuEnv},
	}
	noTestPathSpec := TestSpec{
		Test: Test{
			Name: "//obsidian/public/lib/baz:baz_tests",
			OS:   Linux,
		},
		Envs: []Environment{qemuEnv},
	}
	noOSSpec := TestSpec{
		Test: Test{
			Name: "//obsidian/bin/foo:foo_unittests",
			Path: "/system/test/foo_unittests",
		},
	}
	badEnvSpec := TestSpec{
		Test: Test{
			Name: "//obsidian/public/lib/baz:baz_tests",
			Path: "/system/test/baz_tests",
			OS:   Linux,
		},
		Envs: []Environment{
			Environment{
				Dimensions: DimensionSet{
					DeviceType: "NON-EXISTENT-DEVICE",
				},
			},
		},
	}
	platforms := []DimensionSet{qemuPlatform, nucPlatform}

	t.Run("valid specs are validated", func(t *testing.T) {
		validSpecLists := [][]TestSpec{
			{specFoo1}, {specFoo2}, {specBar},
			{specFoo1, specFoo2}, {specFoo1, specBar}, {specFoo2, specBar},
			{specFoo1, specFoo2, specBar},
		}
		for _, list := range validSpecLists {
			if err := ValidateTestSpecs(list, platforms); err != nil {
				t.Fatalf("valid specs marked as invalid: %+v: %v", list, err)
			}
		}
	})

	t.Run("invalid specs are invalidated", func(t *testing.T) {
		invalidSpecLists := [][]TestSpec{
			{noOSSpec}, {noTestNameSpec}, {noTestPathSpec}, {badEnvSpec},
			{noTestNameSpec, noTestPathSpec}, {noTestNameSpec, badEnvSpec},
			{noTestPathSpec, badEnvSpec},
			{noTestNameSpec, noTestPathSpec, badEnvSpec},
		}
		for _, list := range invalidSpecLists {
			if err := ValidateTestSpecs(list, platforms); err == nil {
				t.Fatalf("invalid specs marked as valid: %+v", list)
			}
		}
	})
}
