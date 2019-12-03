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

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

var qemuPlatform = build.DimensionSet{
	DeviceType: "QEMU",
}

var nucPlatform = build.DimensionSet{
	DeviceType: "NUC",
}

var linuxPlatform = build.DimensionSet{
	OS: "Linux",
}

var macPlatform = build.DimensionSet{
	OS: "Mac",
}

var qemuEnv = build.Environment{
	Dimensions: qemuPlatform,
}

var nucEnv = build.Environment{
	Dimensions: nucPlatform,
}

var linuxEnv = build.Environment{
	Dimensions: linuxPlatform,
}

var macEnv = build.Environment{
	Dimensions: macPlatform,
}

var specFoo1 = build.TestSpec{
	Test: build.Test{
		Name:    "foo_unittests",
		Label:   "//obsidian/bin/foo:foo_unittests(//build/toolchain/fuchsia:x64",
		Path:    "/system/test/foo_unittests",
		OS:      "fuchsia",
		Command: []string{"/system/test/foo_unittests", "bar", "baz"},
	},
	Envs: []build.Environment{qemuEnv},
}

var specFoo2 = build.TestSpec{
	Test: build.Test{
		Name:  "//obsidian/bin/foo:foo_integration_tests",
		Label: "//obsidian/bin/foo:foo_integration_tests(//build/toolchain/fuchsia:x64",
		Path:  "/system/test/foo_integration_tests",
		OS:    "fuchsia",
	},
	Envs: []build.Environment{qemuEnv, nucEnv},
}

var specBar = build.TestSpec{
	Test: build.Test{
		Name:  "//obsidian/lib/bar:bar_tests",
		Label: "//obsidian/lib/bar:bar_tests(//build/toolchain/fuchsia:x64",
		Path:  "/system/test/bar_tests",
		OS:    "fuchsia",
	},
	Envs: []build.Environment{qemuEnv},
}

var specBaz = build.TestSpec{
	Test: build.Test{
		Name:  "//obsidian/public/lib/baz:baz_host_tests",
		Label: "//obsidian/public/lib/baz:baz_host_tests(//build/toolchain/fuchsia:x64",
		Path:  "/$root_build_dir/baz_host_tests",
		OS:    "linux",
	},
	Envs: []build.Environment{linuxEnv, macEnv},
}

func TestLoadTestSpecs(t *testing.T) {
	areEqual := func(a, b []build.TestSpec) bool {
		stringify := func(spec build.TestSpec) string {
			return fmt.Sprintf("%#v", spec)
		}
		sort := func(list []build.TestSpec) {
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
	specBazIn.RuntimeDepsFile = "deps.json"
	initial := []build.TestSpec{specBar, specBazIn}

	manifest := filepath.Join(tmpDir, build.TestModuleName)
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
	expected := []build.TestSpec{specBar, specBazOut}

	if !areEqual(expected, actual) {
		t.Fatalf("test specs not properly loaded:\nexpected:\n%+v\nactual:\n%+v", expected, actual)
	}
}

func TestValidateTests(t *testing.T) {
	noTestNameSpec := build.TestSpec{
		Test: build.Test{
			Label: "//obsidian/public/lib/baz:baz_tests(//toolchain)",
			Path:  "/system/test/baz_tests",
			OS:    "linux",
		},
		Envs: []build.Environment{qemuEnv},
	}
	noTestLabelSpec := build.TestSpec{
		Test: build.Test{
			Name: "something_tests",
			Path: "/system/test/something_tests",
			OS:   "linux",
		},
		Envs: []build.Environment{qemuEnv},
	}
	noTestPathSpec := build.TestSpec{
		Test: build.Test{
			Name:  "baz_tests",
			Label: "//obsidian/public/lib/baz:baz_tests(//toolchain)",
			OS:    "linux",
		},
		Envs: []build.Environment{qemuEnv},
	}
	noOSSpec := build.TestSpec{
		Test: build.Test{
			Name:  "foo_unittests",
			Label: "//obsidian/bin/foo:foo_unittests(//toolchain)",
			Path:  "/system/test/foo_unittests",
		},
	}
	badEnvSpec := build.TestSpec{
		Test: build.Test{
			Name:  "baz_tests",
			Label: "//obsidian/public/lib/baz:baz_tests(//toolchain)",
			Path:  "/system/test/baz_tests",
			OS:    "linux",
		},
		Envs: []build.Environment{
			{
				Dimensions: build.DimensionSet{
					DeviceType: "NON-EXISTENT-DEVICE",
				},
			},
		},
	}
	platforms := []build.DimensionSet{qemuPlatform, nucPlatform}

	t.Run("valid specs are validated", func(t *testing.T) {
		validSpecLists := [][]build.TestSpec{
			{specFoo1}, {specFoo2}, {specBar},
			{specFoo1, specFoo2}, {specFoo1, specBar}, {specFoo2, specBar},
			{specFoo1, specFoo2, specBar},
		}
		for _, list := range validSpecLists {
			if err := ValidateTests(list, platforms); err != nil {
				t.Fatalf("valid specs marked as invalid: %+v: %v", list, err)
			}
		}
	})

	t.Run("invalid specs are invalidated", func(t *testing.T) {
		invalidSpecLists := [][]build.TestSpec{
			{noOSSpec}, {noTestNameSpec}, {noTestPathSpec}, {noTestLabelSpec},
			{badEnvSpec}, {noTestNameSpec, noTestPathSpec},
			{noTestNameSpec, badEnvSpec}, {noTestPathSpec, badEnvSpec},
			{noTestNameSpec, noTestPathSpec, badEnvSpec},
		}
		for _, list := range invalidSpecLists {
			if err := ValidateTests(list, platforms); err == nil {
				t.Fatalf("invalid specs marked as valid: %+v", list)
			}
		}
	})
}
