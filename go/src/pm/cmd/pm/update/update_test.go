// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package update

import (
	"crypto/rand"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"io"

	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/merkle"
	"fuchsia.googlesource.com/pm/testpackage"
)

func TestRun(t *testing.T) {
	tmpDir, err := testpackage.New()
	defer os.RemoveAll(tmpDir)
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range testpackage.Files {
		fd, err := os.Create(filepath.Join(tmpDir, f))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.CopyN(fd, rand.Reader, 1024); err != nil {
			t.Fatal(err)
		}
		fd.Close()
	}

	if err := initcmd.Run(tmpDir); err != nil {
		t.Fatal(err)
	}

	if err := Run(tmpDir); err != nil {
		t.Fatal(err)
	}

	f, err := os.Open(filepath.Join(tmpDir, "meta", "contents"))
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

	// meta/package.json is also included
	if len(lines) != len(testpackage.Files)+1 {
		t.Fatalf("content lines mismatch: %v\n%v", lines, testpackage.Files)
	}

	files := []string{}
	files = append(files, testpackage.Files...)
	files = append(files, "meta/package.json")
	for i, pf := range files {
		var tree merkle.Tree
		f, err := os.Open(filepath.Join(tmpDir, pf))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := tree.ReadFrom(f); err != nil {
			t.Fatal(err)
		}
		f.Close()
		want := fmt.Sprintf("%s:%x", pf, tree.Root())
		if lines[i] != want {
			t.Errorf("contents mismatch: got %q, want %q", lines[i], want)
			break
		}
	}
}
