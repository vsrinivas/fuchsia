// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestLicensesMatchSingleLicenseFile(t *testing.T) {
	// TODO(http://fxbug.dev/56847): Implement.
}

func TestLicensesMatchFile(t *testing.T) {
	// TODO(http://fxbug.dev/56847): Implement.
}

func TestLicensesInit(t *testing.T) {
	folder := mkDir(t)
	path := filepath.Join(folder, "test.lic")
	if err := ioutil.WriteFile(path, []byte("abc"), 0600); err != nil {
		t.Fatal(err)
	}
	var licenses Licenses
	if err := licenses.Init(folder); err != nil {
		t.Error("error: licenses.Init()")
	}
}

func TestLicensesNew(t *testing.T) {
	folder := mkDir(t)
	path := filepath.Join(folder, "test.lic")
	if err := ioutil.WriteFile(path, []byte("abc"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := NewLicenses(folder); err != nil {
		t.Error("error: NewLicenses(...)")
	}
}

func TestLicensesWorker(t *testing.T) {
	// TODO(http://fxbug.dev/56847): Implement.
}

func mkDir(t *testing.T) string {
	name, err := ioutil.TempDir("", "check-licenses")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := os.RemoveAll(name); err != nil {
			t.Error(err)
		}
	})
	return name
}
