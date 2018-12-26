// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist_test

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"syscall"
	"testing"

	"fuchsia.googlesource.com/tools/botanist"
)

func TestCopyFile(t *testing.T) {
	dir, err := ioutil.TempDir("", "botanist")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dir)

	src := filepath.Join(dir, "src")
	if err = ioutil.WriteFile(src, []byte("foo"), 0666); err != nil {
		t.Fatal(err)
	}

	dest := filepath.Join(dir, "dest")
	if err := botanist.CopyFile(src, dest); err != nil {
		t.Fatalf("copy failed: %v", err)
	}

	bytes, err := ioutil.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if "foo" != string(bytes) {
		t.Fatalf("destination had unexpected content: %s", bytes)
	}
}

func TestOverwriteFileWithCopy(t *testing.T) {
	getInode := func(t *testing.T, path string) uint64 {
		info, err := os.Stat(path)
		if err != nil {
			t.Fatal(err)
		}
		stat, ok := info.Sys().(*syscall.Stat_t)
		if !ok {
			t.Fatalf("could not cast to *syscall.Stat_t")
		}
		return stat.Ino
	}

	file, err := ioutil.TempFile("", "botanist")
	if err != nil {
		t.Fatal(err)
	}
	inode1 := getInode(t, file.Name())
	if err := botanist.OverwriteFileWithCopy(file.Name()); err != nil {
		t.Fatalf("failed to overwrite file with copy %v", err)
	}
	inode2 := getInode(t, file.Name())
	if inode1 == inode2 {
		t.Fatal("files before and after have the same inode")
	}
}
