// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package init

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pm/testpackage"
)

func TestRun(t *testing.T) {
	tmpDir, err := testpackage.New()
	defer os.RemoveAll(tmpDir)
	if err != nil {
		t.Fatal(err)
	}

	Run(tmpDir)

	f, err := os.Open(filepath.Join(tmpDir, "meta", "package.json"))
	if err != nil {
		t.Fatal(err)
	}
	var p pkg.Package
	if err := json.NewDecoder(f).Decode(&p); err != nil {
		t.Fatal(err)
	}
	f.Close()

	if want := filepath.Base(tmpDir); p.Name != want {
		t.Errorf("got %q, want %q", p.Name, want)
	}

	if want := "0"; p.Version != want {
		t.Errorf("got %q, want %q", p.Version, want)
	}

	f, err = os.Open(filepath.Join(tmpDir, "meta", "contents"))
	if err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadAll(f)
	if err != nil {
		t.Fatal(err)
	}

	lines := strings.Split(string(buf), "\n")
	sort.Strings(lines)
	if lines[0] == "" {
		lines = lines[1:]
	}

	for i, pf := range testpackage.Files {
		if lines[i] != pf {
			t.Errorf("contents mismatch: got %q, want %q", lines[i], pf)
			break
		}
	}
}
