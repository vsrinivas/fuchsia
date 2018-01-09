// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"fuchsia.googlesource.com/pm/pkg"
)

func TestNew(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	// assert that a pre-existing directory is fine
	_, err = New(d)
	if err != nil {
		t.Fatal(err)
	}

	// assert that a non-existing directory is fine
	err = os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	_, err = New(d)
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

	idx, err := New(d)
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

	idx, err := New(d)
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

	idx, err := New(d)
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
