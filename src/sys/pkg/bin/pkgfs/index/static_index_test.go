// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"fmt"
	"io/ioutil"
	"os"
	"reflect"
	"sort"
	"testing"

	"fuchsia.googlesource.com/pm/pkg"
)

func setUpStaticIndex(t *testing.T) (*StaticIndex, pkg.Package) {
	f, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(f.Name())

	fmt.Fprintf(f, "a/0=331e2e4b22e61fba85c595529103f957d7fe19731a278853361975d639a1bdd8\n")
	f.Seek(0, os.SEEK_SET)

	si := NewStatic()
	systemImage := pkg.Package{
		Name:    "system_image",
		Version: "0",
	}
	err = si.LoadFrom(f, systemImage, "2222222222222222222222222222222222222222222222222222222222222222")
	if err != nil {
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
