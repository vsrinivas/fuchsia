// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package pkgfs

import (
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pmd/ramdisk"
)

// Adding a file to /in writes the file to blobfs
// Adding a file that is a meta.far to /in creates the package in the package filesystem
// If not all of a packages contents are available, opening the package directory should fail
// A package directory should contain all files from meta.far and listed by meta/contents

var (
	pkgfsMount string
	blobfsPath string
)

// tmain exists for the defer convenience, so that defers are run before os.Exit gets called.
func tmain(m *testing.M) int {
	// Undo the defaults that print to the system log...
	log.SetOutput(os.Stdout)

	var err error
	blobfsPath, err = ioutil.TempDir("", "pkgfs-test-blobfs")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(blobfsPath)

	staticFile, err := ioutil.TempFile("", "pkgfs-test-static-index")
	if err != nil {
		panic(err)
	}
	fmt.Fprintf(staticFile, "static-package/0=331e2e4b22e61fba85c595529103f957d7fe19731a278853361975d639a1bdd8\n")
	staticFile.Close()
	staticPath := staticFile.Name()
	defer os.RemoveAll(staticPath)

	indexPath, err := ioutil.TempDir("", "pkgfs-test-index")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(indexPath)

	rd, err := ramdisk.New(10 * 1024 * 1024)
	if err != nil {
		panic(err)
	}
	defer rd.Destroy()

	if err := rd.MkfsBlobfs(); err != nil {
		panic(err)
	}

	if err := rd.MountBlobfs(blobfsPath); err != nil {
		panic(err)
	}
	defer rd.Umount(blobfsPath)

	fmt.Printf("blobfs mounted at %s\n", blobfsPath)

	d, err := ioutil.TempDir("", "pkgfs-test-mount")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(d)

	pkgfs, err := New(indexPath, blobfsPath)
	if err != nil {
		panic(err)
	}
	sf, err := os.Open(staticPath)
	if err != nil {
		panic(err)
	}
	pkgfs.static.LoadFrom(sf)
	sf.Close()
	go func() {
		if err := pkgfs.Mount(d); err != nil {
			panic(err)
		}
	}()
	defer pkgfs.Unmount()
	pkgfsMount = d

	return m.Run()
}

func TestMain(m *testing.M) {
	println("starting tests")
	v := tmain(m)
	println("cleaned up tests")
	os.Exit(v)
}

func TestAddPackage(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.TestPackage(cfg)

	var err error

	err = build.Update(cfg)
	if err != nil {
		t.Fatal(err)
	}
	err = build.Sign(cfg)
	if err != nil {
		t.Fatal(err)
	}
	_, err = build.Seal(cfg)
	if err != nil {
		t.Fatal(err)
	}

	src, err := os.Open(filepath.Join(cfg.OutputDir, "meta.far"))
	if err != nil {
		t.Fatal(err)
	}

	var tree merkle.Tree

	_, err = tree.ReadFrom(src)
	if err != nil {
		t.Error(err)
	}
	merkleroot := fmt.Sprintf("%x", tree.Root())
	fi, err := src.Stat()
	if err != nil {
		t.Fatal(err)
	}

	f, err := os.Create(filepath.Join(pkgfsMount, "install", "pkg", merkleroot))
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(fi.Size()); err != nil {
		t.Fatal(err)
	}
	src.Seek(0, os.SEEK_SET)
	if _, err := io.Copy(f, src); err != nil {
		t.Fatal(err)
	}
	src.Close()
	err = f.Close()
	if err != nil {
		t.Fatal(err)
	}

	_, err = os.Stat(filepath.Join(blobfsPath, merkleroot))
	if err != nil {
		t.Fatalf("package blob missing after package write: %s", err)
	}

	// TODO(raggi): check that the pacakge content blobs appear in the needs tree
	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): extract into constant in testutil
	packageName := "testpackage"
	packageVersion := "0"

	if _, err = os.Stat(filepath.Join(pkgfsMount, "packages", packageName, packageVersion)); err == nil {
		t.Error("package appeared in the pkgfs package tree before needs fulfilled")
	}

	needs, err := filepath.Glob(filepath.Join(pkgfsMount, "needs", "blobs", "*"))
	if err != nil {
		t.Fatal(err)
	}
	for i := range needs {
		needs[i] = filepath.Base(needs[i])
	}
	sort.Strings(needs)

	contents, err := ioutil.ReadFile(manifest.Paths["meta/contents"])
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(string(contents), "\n")
	for _, line := range lines {
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}
		name := parts[0]
		root := parts[1]
		idx := sort.SearchStrings(needs, root)
		if idx == len(needs) {
			t.Errorf("need of blob %q (file %q) not found in needs glob: %v", root, name, needs)
			continue
		}

		// write the real content into the target to fulfill the need
		err := copyBlob(filepath.Join(pkgfsMount, "install", "blob", root), manifest.Paths[name])
		if err != nil {
			t.Fatal(err)
		}
	}

	var info os.FileInfo
	if info, err = os.Stat(filepath.Join(pkgfsMount, "packages", packageName)); err != nil {
		t.Fatalf("package did not appear in the pkgfs package tree: %s", err)
	}
	if !info.IsDir() {
		t.Errorf("os.Stat on package directory says it's not a directory")
	}
	if info, err = os.Stat(filepath.Join(pkgfsMount, "packages", packageName, packageVersion)); err != nil {
		t.Fatalf("package version did not appear in the pkgfs package tree: %s", err)
	}
	if !info.IsDir() {
		t.Errorf("os.Stat on package version directory says it's not a directory")
	}

	// put the files into needs and expect the pacakage to be live

	for f := range manifest.Content() {
		b, err := ioutil.ReadFile(filepath.Join(pkgfsMount, "packages", packageName, packageVersion, f))
		if err != nil {
			t.Fatal(err)
		}
		if got, want := string(b), f+"\n"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

func TestListContainsStatic(t *testing.T) {
	names, err := filepath.Glob(filepath.Join(pkgfsMount, "packages", "*", "*"))
	if err != nil {
		t.Fatal(err)
	}
	name := ""
	for _, path := range names {
		if strings.Contains(path, "static-") {
			name = path
		}
	}

	want := "static-package/0"
	if !strings.HasSuffix(name, want) {
		t.Errorf("did not find %q in %v (%q)", want, names, name)
	}
}

func TestListRoot(t *testing.T) {
	names, err := filepath.Glob(filepath.Join(pkgfsMount, "*"))
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"install", "needs", "packages", "system", "metadata"}
	sort.Strings(names)
	sort.Strings(want)

	if len(names) != len(want) {
		t.Fatalf("got %v, want %v", names, want)
	}

	for i, name := range names {
		got := filepath.Base(name)
		if want := want[i]; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}

}

func copyBlob(dest, src string) error {
	d, err := os.Create(dest)
	if err != nil {
		return err
	}
	defer d.Close()
	s, err := os.Open(src)
	if err != nil {
		return err
	}
	defer s.Close()
	info, err := s.Stat()
	if err != nil {
		return err
	}
	d.Truncate(info.Size())
	if _, err := io.Copy(d, s); err != nil {
		return err
	}
	return d.Close()
}
