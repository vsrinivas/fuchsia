// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"fmt"
	"io/ioutil"
	"os"
	"reflect"
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
	f.Seek(0, os.SEEK_SET)

	si := NewStatic()
	err = si.LoadFrom(f)
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

	// attempt to set package "a" to the merkleroot all-a's, which must not be visible to the loop below.
	if err := si.Set(pkg.Package{"a", "0"}, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); err != nil {
		t.Fatal(err)
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

	// check that the setpackage to all "a"'s above is still in the update's set,
	// so will be preserved by GC
	if _, got := si.GetRoot("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); got != true {
		t.Errorf("prior index set not observable")
	}
}
