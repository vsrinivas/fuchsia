// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestLicensesMatchSingleLicenseFile(t *testing.T) {
	folder := mkDir(t)
	l, err := NewLicenses(context.Background(), folder, []string{"gcc"})
	if err != nil {
		t.Fatalf("NewLicenses(...): %s", err)
	}
	metrics := &Metrics{}
	metrics.Init()
	ft := &FileTree{}
	ft.Init()
	data := []byte("This is very Apache licensed\nCopyright Foo\n")
	l.MatchSingleLicenseFile(data, "foo.rs", metrics, ft)
	data = []byte("BSD much.\nCopyright Bar Inc\n")
	l.MatchSingleLicenseFile(data, "bar.rs", metrics, ft)
	if metrics.values["num_single_license_file_match"] != 2 {
		t.Error(metrics.values["num_single_license_file_match"])
	}
}

func TestLicensesMatchFile(t *testing.T) {
	folder := mkDir(t)
	l, err := NewLicenses(context.Background(), folder, []string{"gcc"})
	if err != nil {
		t.Fatalf("NewLicenses(...): %s", err)
	}
	metrics := &Metrics{}
	metrics.Init()
	data := []byte("This is very Apache licensed\nCopyright Foo\n")
	ok, _ := l.MatchFile(data, "foo.rs", metrics)
	if !ok {
		t.Error("Apache didn't match")
	}
	data = []byte("BSD much.\nCopyright Bar Inc\n")
	ok, _ = l.MatchFile(data, "bar.rs", metrics)
	if !ok {
		t.Error("Apache didn't match")
	}
	if metrics.values["num_licensed"] != 2 {
		t.Error(metrics.values["num_licensed"])
	}
}

func TestNewLicenses(t *testing.T) {
	folder := mkDir(t)
	l, err := NewLicenses(context.Background(), folder, []string{"gcc"})
	if err != nil {
		t.Fatalf("NewLicenses(...): %s", err)
	}
	if len(l.licenses) != 2 {
		t.Fatalf("Got %#v", l.licenses)
	}
	// bsd comes first because it is shorter.
	if l.licenses[0].Category != "bsd.lic" {
		t.Fatalf("Got %#v", l.licenses[0])
	}
	if l.licenses[1].Category != "apache.lic" {
		t.Fatalf("Got %#v", l.licenses[0])
	}
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
	if err := ioutil.WriteFile(filepath.Join(name, "apache.lic"), []byte("Apache"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(filepath.Join(name, "bsd.lic"), []byte("BSD"), 0600); err != nil {
		t.Fatal(err)
	}
	return name
}
