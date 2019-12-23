// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"reflect"
	"testing"

	"fuchsia.googlesource.com/pm/pkg"
)

func TestAdd(t *testing.T) {
	idx := NewDynamic(NewStatic())

	err := idx.Add(pkg.Package{Name: "foo", Version: "0"}, "abc")
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "foo", Version: "1"}, "def")
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "bar", Version: "10"}, "123")
	if err != nil {
		t.Fatal(err)
	}

	pkgs := idx.List()
	wantPkgs := []pkg.Package{{Name: "foo", Version: "0"}, {Name: "foo", Version: "1"}, {Name: "bar", Version: "10"}}
	if !reflect.DeepEqual(pkgs, wantPkgs) {
		t.Errorf("got %q, want %q", pkgs, wantPkgs)
	}
}

func TestList(t *testing.T) {
	idx := NewDynamic(NewStatic())

	pkgs := []pkg.Package{
		{"foo", "0"},
		{"bar", "1"},
	}
	roots := []string{"abc", "def"}
	for i, pkg := range pkgs {
		if err := idx.Add(pkg, roots[i]); err != nil {
			t.Fatal(err)
		}
	}

	list := idx.List()
	if got, want := len(list), len(pkgs); got != want {
		t.Errorf("got %d, want %d", got, want)
	}
	for i, pkg := range pkgs {
		if !reflect.DeepEqual(list[i], pkg) {
			t.Errorf("mismatched package at %d: %v, %v", i, list[i], pkg)
		}
	}
}

func TestFulfill(t *testing.T) {
	idx := NewDynamic(NewStatic())

	// New package, no blobs pre-installed.
	{
		// Start installing package.
		neededBlobs := map[string]struct{}{
			"blob1": {},
			"blob2": {},
		}
		idx.Installing("root1")
		idx.UpdateInstalling("root1", pkg.Package{Name: "foo", Version: "1"})
		idx.AddNeeds("root1", neededBlobs)

		wantWaiting := map[string]struct{}{
			"blob1": {},
			"blob2": {},
		}
		if !reflect.DeepEqual(idx.waiting["root1"], wantWaiting) {
			t.Errorf("got %q, want %q", idx.waiting["root1"], wantWaiting)
		}
		wantNeeds := map[string]map[string]struct{}{
			"blob1": {"root1": {}},
			"blob2": {"root1": {}},
		}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		wantInstalling := pkg.Package{Name: "foo", Version: "1"}
		gotInstalling := idx.installing["root1"]
		if !reflect.DeepEqual(gotInstalling, wantInstalling) {
			t.Errorf("got %v, want %v", gotInstalling, wantInstalling)
		}

		// Fulfill one blob. Package is not done yet.
		idx.Fulfill("blob1")

		wantWaiting = make(map[string]struct{})
		wantWaiting["blob2"] = struct{}{}
		if !reflect.DeepEqual(idx.waiting["root1"], wantWaiting) {
			t.Errorf("got %q, want %q", idx.waiting["root1"], wantWaiting)
		}
		wantNeeds = map[string]map[string]struct{}{
			"blob2": {"root1": {}},
		}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}

		// Fulfill other blob. Package is done and appears in index.
		idx.Fulfill("blob2")
		if _, ok := idx.waiting["root1"]; ok {
			t.Errorf("root1 was not deleted from waiting")
		}
		wantNeeds = map[string]map[string]struct{}{}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		if _, ok := idx.installing["root1"]; ok {
			t.Errorf("root1 was not deleted from installing")
		}

		pkgs := idx.List()
		wantPkgs := []pkg.Package{{Name: "foo", Version: "1"}}
		if !reflect.DeepEqual(pkgs, wantPkgs) {
			t.Errorf("got %q, want %q", pkgs, wantPkgs)
		}
	}

	// Second package only needs one blob.
	{
		// Start installing package.
		neededBlobs := map[string]struct{}{
			"blob4": {},
		}
		idx.Installing("root2")
		idx.UpdateInstalling("root2", pkg.Package{Name: "bar", Version: "2"})
		idx.AddNeeds("root2", neededBlobs)

		wantWaiting := map[string]struct{}{
			"blob4": {},
		}
		if !reflect.DeepEqual(idx.waiting["root2"], wantWaiting) {
			t.Errorf("got %q, want %q", idx.waiting, wantWaiting)
		}
		wantNeeds := map[string]map[string]struct{}{
			"blob4": {"root2": {}},
		}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		wantInstalling := pkg.Package{Name: "bar", Version: "2"}
		gotInstalling := idx.installing["root2"]
		if !reflect.DeepEqual(gotInstalling, wantInstalling) {
			t.Errorf("got %v, want %v", gotInstalling, wantInstalling)
		}

		// Fulfill blob. Now the package should be marked finished.
		idx.Fulfill("blob4")

		if _, ok := idx.waiting["root2"]; ok {
			t.Errorf("root2 was not deleted from waiting")
		}
		wantNeeds = map[string]map[string]struct{}{}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		if _, ok := idx.installing["root2"]; ok {
			t.Errorf("root2 was not deleted from installing")
		}
	}

	// This is getting out of hand; now there are two of them
	pkgs := idx.List()
	wantPkgs := []pkg.Package{{Name: "foo", Version: "1"}, {Name: "bar", Version: "2"}}
	if !reflect.DeepEqual(pkgs, wantPkgs) {
		t.Errorf("got %q, want %q", pkgs, wantPkgs)
	}
}
