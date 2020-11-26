// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"reflect"
	"sort"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
)

const lotsOf2s = "2222222222222222222222222222222222222222222222222222222222222222"

func setUpStaticIndex(t *testing.T) (*StaticIndex, pkg.Package) {
	f := strings.NewReader("a/0=331e2e4b22e61fba85c595529103f957d7fe19731a278853361975d639a1bdd8\n")
	si := NewStatic()
	systemImage := pkg.Package{
		Name:    "system_image",
		Version: "0",
	}
	if err := si.LoadFrom(f, systemImage, lotsOf2s); err != nil {
		t.Fatal(err)
	}
	return si, systemImage
}

func TestStatic(t *testing.T) {
	si, systemImage := setUpStaticIndex(t)

	expectList := []pkg.Package{{Name: "a", Version: "0"}, systemImage}
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
		if got, _ := si.Get(pkg.Package{Name: tc.name, Version: tc.version}); got != tc.result {
			t.Errorf("static.Get(%q, %q) = %q want %q", tc.name, tc.version, got, tc.result)
		}
	}

	if got, want := si.ListVersions("a"), []string{"0"}; !reflect.DeepEqual(got, want) {
		t.Errorf("static.ListVersions(`a`) = %v, want %v", got, want)
	}
}

func TestStaticUpdatesNotShownInList(t *testing.T) {
	si, systemImage := setUpStaticIndex(t)

	si.Set(pkg.Package{Name: "a", Version: "1"}, "1111111111111111111111111111111111111111111111111111111111111111")
	expectList := []pkg.Package{{Name: "a", Version: "0"}, systemImage}
	gotList, err := si.List()

	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(gotList, expectList) {
		t.Errorf("static.List() %v != %v", gotList, expectList)
	}
}

func TestStaticUpdatesNotShownInGet(t *testing.T) {
	si, _ := setUpStaticIndex(t)

	si.Set(pkg.Package{Name: "a", Version: "1"}, "1111111111111111111111111111111111111111111111111111111111111111")

	getPackageCases := []struct {
		name, version string
		result        string
	}{
		{"a", "0", "331e2e4b22e61fba85c595529103f957d7fe19731a278853361975d639a1bdd8"},
		{"a", "1", ""},
		{"b", "0", ""},
	}
	for _, tc := range getPackageCases {
		if got, _ := si.Get(pkg.Package{Name: tc.name, Version: tc.version}); got != tc.result {
			t.Errorf("static.Get(%q, %q) = %q want %q", tc.name, tc.version, got, tc.result)
		}
	}
}

func TestStaticUpdatesShownInListVersions(t *testing.T) {
	si, _ := setUpStaticIndex(t)

	si.Set(pkg.Package{Name: "a", Version: "1"}, "1111111111111111111111111111111111111111111111111111111111111111")
	got, want := si.ListVersions("a"), []string{"0", "1"}
	sort.Strings(got)

	if !reflect.DeepEqual(got, want) {
		t.Errorf("static.ListVersions(`a`) = %v, want %v", got, want)
	}
}

func TestHasName(t *testing.T) {
	f := strings.NewReader(
		"static/0=0000000000000000000000000000000000000000000000000000000000000000\n" +
			"update/0=0000000000000000000000000000000000000000000000000000000000000001\n")
	si := NewStatic()
	systemImage := pkg.Package{
		Name:    "system_image",
		Version: "0",
	}
	if err := si.LoadFrom(f, systemImage, lotsOf2s); err != nil {
		t.Fatal(err)
	}

	if err := si.Set(pkg.Package{Name: "update", Version: "0"}, "0000000000000000000000000000000000000000000000000000000000000002"); err != nil {
		t.Fatal(err)
	}
	if err := si.Set(pkg.Package{Name: "new", Version: "0"}, "0000000000000000000000000000000000000000000000000000000000000003"); err != nil {
		t.Fatal(err)
	}

	hasNameCases := []struct {
		name         string
		static, both bool
	}{
		{"static", true, true},
		{"update", true, true},
		{"new", false, true},
		{"unknown", false, false},
	}
	for _, tc := range hasNameCases {
		if got := si.HasStaticName(tc.name); got != tc.static {
			t.Errorf("static.HasStaticName(%q) = %v want %v", tc.name, got, tc.static)
		}

		if got := si.HasName(tc.name); got != tc.both {
			t.Errorf("static.HasName(%q) = %v want %v", tc.name, got, tc.both)
		}
	}
}

func TestHasStaticRoot(t *testing.T) {
	f := strings.NewReader(
		"static/0=0000000000000000000000000000000000000000000000000000000000000000\n" +
			"update/0=0000000000000000000000000000000000000000000000000000000000000001\n")
	si := NewStatic()
	systemImage := pkg.Package{
		Name:    "system_image",
		Version: "0",
	}

	if err := si.LoadFrom(f, systemImage, lotsOf2s); err != nil {
		t.Fatal(err)
	}

	if err := si.Set(pkg.Package{Name: "update", Version: "0"}, "0000000000000000000000000000000000000000000000000000000000000002"); err != nil {
		t.Fatal(err)
	}
	if err := si.Set(pkg.Package{Name: "new", Version: "0"}, "0000000000000000000000000000000000000000000000000000000000000003"); err != nil {
		t.Fatal(err)
	}

	cases := []struct {
		root          string
		hasStaticRoot bool
	}{
		{"0000000000000000000000000000000000000000000000000000000000000000", true},
		{"0000000000000000000000000000000000000000000000000000000000000001", true},
		{"0000000000000000000000000000000000000000000000000000000000000002", false},
		{"0000000000000000000000000000000000000000000000000000000000000003", false},
		{"0000000000000000000000000000000000000000000000000000000000000004", false},
	}
	for _, tc := range cases {
		if got := si.HasStaticRoot(tc.root); got != tc.hasStaticRoot {
			t.Errorf("static.HasRootName(%q) = %v want %v", tc.root, got, tc.hasStaticRoot)
		}
	}
}
