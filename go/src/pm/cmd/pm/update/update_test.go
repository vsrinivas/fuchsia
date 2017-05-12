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
)

func TestRun(t *testing.T) {
	tmpDir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(tmpDir)

	packageFiles := []string{"a", "b", "dir/c"}
	sort.Strings(packageFiles)

	for _, f := range packageFiles {
		if err := os.MkdirAll(filepath.Join(tmpDir, filepath.Dir(f)), os.ModePerm); err != nil {
			t.Fatal(err)
		}

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

	if len(lines) != len(packageFiles) {
		t.Fatalf("content lines mismatch: %v\n%v", lines, packageFiles)
	}

	for i, pf := range packageFiles {
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
