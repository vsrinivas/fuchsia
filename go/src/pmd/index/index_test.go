// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"testing"

	"fuchsia.googlesource.com/pm/pkg"
)

func TestStatic(t *testing.T) {
	f, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(f.Name())

	fmt.Fprintf(f, "a/0=331e2e4b22e61fba85c595529103f957d7fe19731a278853361975d639a1bdd8\n")
	f.Close()

	si, err := LoadStaticIndex(f.Name())
	if err != nil {
		t.Fatal(err)
	}

	expectList := []pkg.Package{{Name: "a", Version: "0"}}
	gotList, err := si.List()
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(gotList, expectList) {
		t.Errorf("static.List() %v != %v", gotList, expectList)
	}

	if !si.HasName("a") {
		t.Error("static.HasName(`a`) = false, want true")
	}

	if si.HasName("b") {
		t.Error("static.HasName(`b`) = true, want false")
	}

	getPackageCases := []struct {
		name, version string
		result        string
	}{
		{"a", "0", "331e2e4b22e61fba85c595529103f957d7fe19731a278853361975d639a1bdd8"},
		{"a", "1", ""},
		{"b", "0", ""},
	}
	for _, tc := range getPackageCases {
		if got := si.GetPackage(tc.name, tc.version); got != tc.result {
			t.Errorf("static.GetPackage(%q, %q) = %q want %q", tc.name, tc.version, got, tc.result)
		}
	}

	if got, want := si.ListVersions("a"), []string{"0"}; !reflect.DeepEqual(got, want) {
		t.Errorf("static.ListVersions(`a`) = %v, want %v", got, want)
	}

}

func TestNewDynamic(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	// assert that a pre-existing directory is fine
	_, err = NewDynamic(d)
	if err != nil {
		t.Fatal(err)
	}

	// assert that a non-existing directory is fine
	err = os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	_, err = NewDynamic(d)
	if err != nil {
		t.Fatal(err)
	}

	if _, err := os.Stat(d); err != nil {
		t.Errorf("expected directory to have been created, got %s", err)
	}
}

func TestList(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	pkgIndexPath := filepath.Join(d, "packages", "foo")
	err = os.MkdirAll(pkgIndexPath, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}
	err = ioutil.WriteFile(filepath.Join(pkgIndexPath, "0"), []byte{}, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}

	idx, err := NewDynamic(d)
	pkgs, err := idx.List()
	if err != nil {
		t.Fatal(err)
	}

	if got, want := len(pkgs), 1; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	if got, want := pkgs[0].Name, "foo"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[0].Version, "0"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}

	pkgIndexPath = filepath.Join(d, "packages", "bar")
	err = os.MkdirAll(pkgIndexPath, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}
	err = ioutil.WriteFile(filepath.Join(pkgIndexPath, "1"), []byte{}, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}

	pkgs, err = idx.List()
	if err != nil {
		t.Fatal(err)
	}

	if got, want := len(pkgs), 2; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	if got, want := pkgs[0].Name, "bar"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[0].Version, "1"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[1].Name, "foo"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[1].Version, "0"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestAdd(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	idx, err := NewDynamic(d)
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "foo", Version: "0"})
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "foo", Version: "1"})
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "bar", Version: "10"})
	if err != nil {
		t.Fatal(err)
	}

	paths, err := filepath.Glob(filepath.Join(d, "packages/*/*"))
	if err != nil {
		t.Fatal(err)
	}

	sort.Strings(paths)

	for i := range paths {
		paths[i] = strings.TrimPrefix(paths[i], filepath.Join(d, "packages")+"/")
	}

	wantPaths := []string{"bar/10", "foo/0", "foo/1"}

	for i := range paths {
		if got, want := paths[i], wantPaths[i]; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

func TestRemove(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	idx, err := NewDynamic(d)
	if err != nil {
		t.Fatal(err)
	}

	packages := []string{
		"bar/10",
		"foo/1",
	}
	for _, p := range packages {
		err = os.MkdirAll(filepath.Dir(filepath.Join(d, "packages", p)), os.ModePerm)
		if err != nil {
			t.Fatal(err)
		}
		err = ioutil.WriteFile(filepath.Join(d, "packages", p), []byte{}, os.ModePerm)
		if err != nil {
			t.Fatal(err)
		}
	}

	if err := idx.Remove(pkg.Package{Name: "foo", Version: "1"}); err != nil {
		t.Fatal(err)
	}

	paths, err := filepath.Glob(filepath.Join(d, "packages", "*", "*"))
	if err != nil {
		t.Fatal(err)
	}
	sort.Strings(paths)

	if got, want := len(paths), 1; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	if got, want := paths[0], filepath.Join(d, "packages", packages[0]); got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}
