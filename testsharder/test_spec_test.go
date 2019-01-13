// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder_test

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
	"fuchsia.googlesource.com/tools/testsharder"
)

var qemuPlatform = testsharder.DimensionSet{
	DeviceType: "QEMU",
}

var nucPlatform = testsharder.DimensionSet{
	DeviceType: "NUC",
}

var linuxPlatform = testsharder.DimensionSet{
	OS: "Linux",
}

var macPlatform = testsharder.DimensionSet{
	OS: "Mac",
}

var qemuEnv = testsharder.Environment{
	Dimensions: qemuPlatform,
}

var nucEnv = testsharder.Environment{
	Dimensions: nucPlatform,
}

var linuxEnv = testsharder.Environment{
	Dimensions: linuxPlatform,
}

var macEnv = testsharder.Environment{
	Dimensions: macPlatform,
}

var specFoo1 = testsharder.TestSpec{
	Test: testsharder.Test{
		Name:     "//obsidian/bin/foo:foo_unittests",
		Location: "/system/test/foo_unittests",
		OS:       "linux",
		Command:  []string{"/system/test/foo_unittests", "bar", "baz"},
	},
	Envs: []testsharder.Environment{qemuEnv},
}

var specFoo2 = testsharder.TestSpec{
	Test: testsharder.Test{
		Name:     "//obsidian/bin/foo:foo_integration_tests",
		Location: "/system/test/foo_integration_tests",
		OS:       "linux",
	},
	Envs: []testsharder.Environment{qemuEnv, nucEnv},
}

var specBar = testsharder.TestSpec{
	Test: testsharder.Test{
		Name:     "//obsidian/lib/bar:bar_tests",
		Location: "/system/test/bar_tests",
		OS:       "linux",
	},
	Envs: []testsharder.Environment{qemuEnv},
}

var specBaz = testsharder.TestSpec{
	Test: testsharder.Test{
		Name:     "//obsidian/public/lib/baz:baz_host_tests",
		Location: "/$root_build_dir/baz_host_tests",
		OS:       "linux",
	},
	Envs: []testsharder.Environment{linuxEnv, macEnv},
}

// FuchsiaBuildDir is a struct representing the root build directory of a fuchsia
// checkout.
type fuchsiaBuildDir struct {
	root string
	t    *testing.T
}

func newFuchsiaBuildDir(t *testing.T) *fuchsiaBuildDir {
	root, err := ioutil.TempDir("", "fuchsia-build-dir")
	if err != nil {
		t.Fatalf("could not create fuchsia build directory: %v", err)
	}
	return &fuchsiaBuildDir{
		root: root,
		t:    t,
	}
}

// CreateLayout takes a list of package and host test targets, and creates the associated
// directory layout.
func (bd fuchsiaBuildDir) createLayout(pkgs []build.Target, hostTests []build.Target) {
	for _, pkg := range pkgs {
		specDir := testsharder.PkgTestSpecDir(bd.root, pkg)
		if err := os.MkdirAll(specDir, os.ModePerm); err != nil {
			bd.t.Fatalf("could not create test spec directory for package \"%s\": %v", pkg.Name, err)
		}
	}
	for _, hostTest := range hostTests {
		specDir := testsharder.HostTestSpecDir(bd.root, hostTest)
		if err := os.MkdirAll(specDir, os.ModePerm); err != nil {
			bd.t.Fatalf("could not create test spec directory for host test \"%s\": %v", hostTest.Name, err)
		}
	}
}

func writeTestSpec(t *testing.T, spec testsharder.TestSpec, path string) {
	bytes, err := json.Marshal(&spec)
	if err != nil {
		t.Fatal(err)
	}
	if err = ioutil.WriteFile(path, bytes, os.ModePerm); err != nil {
		t.Fatalf("could not write test spec to %s: %v", path, err)
	}
}

func testSpecFilename(basename string) string {
	return basename + testsharder.TestSpecSuffix
}

func TestLoadTestSpecs(t *testing.T) {
	areEqual := func(a, b []testsharder.TestSpec) bool {
		stringify := func(spec testsharder.TestSpec) string {
			return fmt.Sprintf("%#v", spec)
		}
		sort := func(list []testsharder.TestSpec) {
			sort.Slice(list[:], func(i, j int) bool {
				return stringify(list[i]) < stringify(list[j])
			})
		}
		sort(a)
		sort(b)
		return reflect.DeepEqual(a, b)
	}

	pkgFoo := build.Target{
		BuildDir: "obj/obsidian/bin/foo",
		Name:     "foo",
	}
	pkgBar := build.Target{
		BuildDir: "obj/obsidian/lib/bar",
		Name:     "bar",
	}
	hostTestBaz := build.Target{
		BuildDir: "host_x64/obj/obsidian/public/lib/baz",
		Name:     "baz",
	}
	pkgs := []build.Target{pkgFoo, pkgBar}
	hostTests := []build.Target{hostTestBaz}

	correctSpecsLoad := func(t *testing.T, expected []testsharder.TestSpec, fuchsiaBuildDir string) {
		actual, err := testsharder.LoadTestSpecs(fuchsiaBuildDir, pkgs, hostTests)
		if err != nil {
			t.Fatalf("error while loading test specs: %v", err)
		}
		if !areEqual(expected, actual) {
			t.Fatalf("test specs not properly loaded:\nexpected:\n%+v\nactual:\n%+v", expected, actual)
		}
	}

	t.Run("test specs are found", func(t *testing.T) {
		bd := newFuchsiaBuildDir(t)
		defer os.RemoveAll(bd.root)
		bd.createLayout(pkgs, hostTests)

		specDirFoo := testsharder.PkgTestSpecDir(bd.root, pkgFoo)
		specDirBar := testsharder.PkgTestSpecDir(bd.root, pkgBar)
		specDirBaz := testsharder.HostTestSpecDir(bd.root, hostTestBaz)
		writeTestSpec(t, specFoo1, filepath.Join(specDirFoo, testSpecFilename("foo_unittest")))
		writeTestSpec(t, specFoo2, filepath.Join(specDirFoo, testSpecFilename("foo_integration_tests")))
		writeTestSpec(t, specBar, filepath.Join(specDirBar, testSpecFilename("bar_tests")))
		writeTestSpec(t, specBaz, filepath.Join(specDirBaz, testSpecFilename("baz_host_tests")))

		expected := []testsharder.TestSpec{specFoo1, specFoo2, specBar, specBaz}
		correctSpecsLoad(t, expected, bd.root)
	})

	t.Run("test specs in wrong location are ignored", func(t *testing.T) {
		bd := newFuchsiaBuildDir(t)
		defer os.RemoveAll(bd.root)
		bd.createLayout(pkgs, hostTests)

		specDirFoo := testsharder.PkgTestSpecDir(bd.root, pkgFoo)
		specDirBar := testsharder.PkgTestSpecDir(bd.root, pkgBar)
		specDirBaz := testsharder.HostTestSpecDir(bd.root, hostTestBaz)
		nonSpecDir := filepath.Join(bd.root, "other-package")
		if err := os.MkdirAll(nonSpecDir, os.ModePerm); err != nil {
			t.Fatalf("failed to create a directory outside of the package manifest: %v", err)
		}

		writeTestSpec(t, specFoo1, filepath.Join(specDirFoo, testSpecFilename("foo_unittests")))
		writeTestSpec(t, specFoo2, filepath.Join(nonSpecDir, testSpecFilename("other_tests")))
		writeTestSpec(t, specBar, filepath.Join(specDirBar, testSpecFilename("bar_tests")))
		writeTestSpec(t, specBaz, filepath.Join(specDirBaz, testSpecFilename("baz_host_tests")))

		expected := []testsharder.TestSpec{specFoo1, specBar, specBaz}
		correctSpecsLoad(t, expected, bd.root)
	})

	t.Run("test specs with wrong extension are ignored", func(t *testing.T) {
		bd := newFuchsiaBuildDir(t)
		defer os.RemoveAll(bd.root)
		bd.createLayout(pkgs, hostTests)

		specDirFoo := testsharder.PkgTestSpecDir(bd.root, pkgFoo)
		specDirBar := testsharder.PkgTestSpecDir(bd.root, pkgBar)
		specDirBaz := testsharder.HostTestSpecDir(bd.root, hostTestBaz)
		writeTestSpec(t, specFoo1, filepath.Join(specDirFoo, "bad_extension1.json"))
		writeTestSpec(t, specFoo2, filepath.Join(specDirFoo, testSpecFilename("good extension")))
		writeTestSpec(t, specBar, filepath.Join(specDirBar, "bad_extension2.spec"))
		writeTestSpec(t, specBaz, filepath.Join(specDirBaz, testSpecFilename("another_good_extension")))

		expected := []testsharder.TestSpec{specFoo2, specBaz}
		correctSpecsLoad(t, expected, bd.root)
	})

	t.Run("malformed test specs raise error", func(t *testing.T) {
		bd := newFuchsiaBuildDir(t)
		defer os.RemoveAll(bd.root)
		bd.createLayout(pkgs, hostTests)

		specDirFoo := testsharder.PkgTestSpecDir(bd.root, pkgFoo)
		specDirBar := testsharder.PkgTestSpecDir(bd.root, pkgBar)

		writeTestSpec(t, specFoo1, filepath.Join(specDirFoo, testSpecFilename("foo_unittests")))
		if err := ioutil.WriteFile(filepath.Join(specDirFoo, testSpecFilename("foo_integration_tests")),
			[]byte("{I am not a test spec}"), os.ModePerm); err != nil {
			t.Fatalf("could not write malformed test spec: %v", err)
		}
		writeTestSpec(t, specBar, filepath.Join(specDirBar, testSpecFilename("bar_tests")))

		_, err := testsharder.LoadTestSpecs(bd.root, pkgs, hostTests)
		if err == nil {
			t.Fatalf("malformed test spec did not raise an error")
		}
	})

	t.Run("deps are loaded", func(t *testing.T) {
		bd := newFuchsiaBuildDir(t)
		defer os.RemoveAll(bd.root)
		bd.createLayout(pkgs, hostTests)

		specDirBaz := testsharder.HostTestSpecDir(bd.root, hostTestBaz)
		writeTestSpec(t, specBaz, filepath.Join(specDirBaz, testSpecFilename("baz_host_tests")))

		deps := []string{"path/to/dep/1", "path/to/dep/2"}
		DepsPath := filepath.Join(specDirBaz, "baz_host_tests"+testsharder.TestDepsSuffix)
		fd, err := os.Create(DepsPath)
		defer fd.Close()
		if err != nil {
			t.Fatal(err)
		}
		for _, dep := range deps {
			_, err := fd.WriteString(dep + "\n")
			if err != nil {
				t.Fatal(err)
			}
		}
		specBazWithDeps := specBaz
		specBazWithDeps.Test.Deps = deps

		expected := []testsharder.TestSpec{specBazWithDeps}
		correctSpecsLoad(t, expected, bd.root)
	})

}

func TestValidateTestSpecs(t *testing.T) {
	noTestNameSpec := testsharder.TestSpec{
		Test: testsharder.Test{
			Location: "/system/test/baz_tests",
			OS:       "linux",
		},
		Envs: []testsharder.Environment{qemuEnv},
	}
	noTestLocationSpec := testsharder.TestSpec{
		Test: testsharder.Test{
			Name: "//obsidian/public/lib/baz:baz_tests",
			OS:   "linux",
		},
		Envs: []testsharder.Environment{qemuEnv},
	}
	noOSSpec := testsharder.TestSpec{
		Test: testsharder.Test{
			Name:     "//obsidian/bin/foo:foo_unittests",
			Location: "/system/test/foo_unittests",
		},
	}
	badEnvSpec := testsharder.TestSpec{
		Test: testsharder.Test{
			Name:     "//obsidian/public/lib/baz:baz_tests",
			Location: "/system/test/baz_tests",
			OS:       "linux",
		},
		Envs: []testsharder.Environment{
			testsharder.Environment{
				Dimensions: testsharder.DimensionSet{
					DeviceType: "NON-EXISTENT-DEVICE",
				},
			},
		},
	}
	platforms := []testsharder.DimensionSet{qemuPlatform, nucPlatform}

	t.Run("valid specs are validated", func(t *testing.T) {
		validSpecLists := [][]testsharder.TestSpec{
			{specFoo1}, {specFoo2}, {specBar},
			{specFoo1, specFoo2}, {specFoo1, specBar}, {specFoo2, specBar},
			{specFoo1, specFoo2, specBar},
		}
		for _, list := range validSpecLists {
			if err := testsharder.ValidateTestSpecs(list, platforms); err != nil {
				t.Fatalf("valid specs marked as invalid: %+v: %v", list, err)
			}
		}
	})

	t.Run("invalid specs are invalidated", func(t *testing.T) {
		invalidSpecLists := [][]testsharder.TestSpec{
			{noOSSpec}, {noTestNameSpec}, {noTestLocationSpec}, {badEnvSpec},
			{noTestNameSpec, noTestLocationSpec}, {noTestNameSpec, badEnvSpec},
			{noTestLocationSpec, badEnvSpec},
			{noTestNameSpec, noTestLocationSpec, badEnvSpec},
		}
		for _, list := range invalidSpecLists {
			if err := testsharder.ValidateTestSpecs(list, platforms); err == nil {
				t.Fatalf("invalid specs marked as valid: %+v", list)
			}
		}
	})
}
