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

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/merkle"
	"fuchsia.googlesource.com/pm/ramdisk"
)

// Adding a file to /in writes the file to the blobstore
// Adding a file that is a meta.far to /in creates the package in the package filesystem
// If not all of a packages contents are available, opening the package directory should fail
// A package directory should contain all files from meta.far and listed by meta/contents

var (
	pkgfsMount    string
	blobstorePath string
)

// tmain exists for the defer convenience, so that defers are run before os.Exit gets called.
func tmain(m *testing.M) int {
	// Undo the defaults that print to the system log...
	log.SetOutput(os.Stdout)

	var err error
	blobstorePath, err = ioutil.TempDir("", "pkgfs-test-blobstore")
	if err != nil {
		panic(err)
	}
	defer func() {
		println("removing blobstore path")
		os.RemoveAll(blobstorePath)
	}()

	indexPath, err := ioutil.TempDir("", "pkgfs-test-index")
	if err != nil {
		panic(err)
	}
	defer func() {
		println("removing index path")
		os.RemoveAll(indexPath)
	}()

	rd, err := ramdisk.New(10 * 1024 * 1024)
	if err != nil {
		panic(err)
	}
	defer func() {
		println("destroying ramdisk")
		rd.Destroy()
	}()

	if err := rd.MkfsBlobstore(); err != nil {
		panic(err)
	}

	if err := rd.MountBlobstore(blobstorePath); err != nil {
		panic(err)
	}
	defer func() {
		println("unmounting blobstore")
		rd.Umount(blobstorePath)
	}()

	fmt.Printf("blobstore mounted at %s\n", blobstorePath)

	d, err := ioutil.TempDir("", "pkgfs-test-mount")
	if err != nil {
		panic(err)
	}
	defer func() {
		println("removing pkgfs mount dir")
		os.RemoveAll(d)
	}()

	pkgfs, err := New(indexPath, blobstorePath)
	if err != nil {
		panic(err)
	}
	if err := pkgfs.Mount(d); err != nil {
		panic(err)
	}
	println("pkgfs mounted")
	defer func() {
		println("unmounting pkgfs")
		pkgfs.Unmount()
		println("unmount done")
	}()
	pkgfsMount = d

	return m.Run()
}

func TestMain(m *testing.M) {
	println("starting tests")
	v := tmain(m)
	println("cleaned up tests")
	os.Exit(v)
}

func TestAddFile(t *testing.T) {

	// TODO(raggi): randomize this blob
	var tree merkle.Tree
	tree.ReadFrom(strings.NewReader("foo"))
	root := tree.Root()
	path := filepath.Join(blobstorePath, fmt.Sprintf("%x", root))
	os.RemoveAll(path)

	info, err := os.Stat(filepath.Join(pkgfsMount, "incoming"))
	if err != nil {
		t.Fatal(err)
	}

	if !info.IsDir() {
		t.Errorf("expected directory, got %#v", info)
	}

	f, err := os.Create(filepath.Join(pkgfsMount, "incoming", "foo"))
	if err != nil {
		t.Fatal(err)
	}
	_, err = f.Write([]byte("foo"))
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}

	buf, err := ioutil.ReadFile(path)

	if err != nil {
		t.Fatal(err)
	}
	if string(buf) != "foo" {
		t.Errorf("got %q, want %q", string(buf), "foo")
	}
}

func TestCreateNeed(t *testing.T) {
	f, err := os.Create(filepath.Join(pkgfsMount, "needs", "mypkg.far"))
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}

	names, err := ioutil.ReadDir(filepath.Join(pkgfsMount, "needs"))
	if err != nil {
		t.Fatal(err)
	}

	found := false
	for _, info := range names {
		if info.Name() == "mypkg.far" {
			found = true
		}
	}
	if !found {
		t.Fatalf("expected to find package in needs, but did not")
	}
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

	f, err := os.Create(filepath.Join(pkgfsMount, "incoming", "meta.far"))
	if err != nil {
		t.Fatal(err)
	}
	src, err := os.Open(filepath.Join(cfg.OutputDir, "meta.far"))
	if err != nil {
		t.Fatal(err)
	}

	var tree merkle.Tree
	tee := io.TeeReader(src, f)

	_, err = tree.ReadFrom(tee)
	if err != nil {
		t.Error(err)
	}
	src.Close()
	err = f.Close()
	if err != nil {
		t.Fatal(err)
	}

	_, err = os.Stat(filepath.Join(blobstorePath, fmt.Sprintf("%x", tree.Root())))
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

	if _, err = os.Stat(filepath.Join(pkgfsMount, "packages", packageName)); err == nil {
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
		err := copyBlob(filepath.Join(pkgfsMount, "needs", "blobs", root), manifest.Paths[name])
		if err != nil {
			t.Fatal(err)
		}
	}

	if _, err = os.Stat(filepath.Join(pkgfsMount, "packages", packageName)); err != nil {
		t.Fatalf("package did not appear in the pkgfs package tree: %s", err)
	}
	if _, err = os.Stat(filepath.Join(pkgfsMount, "packages", packageName, packageVersion)); err != nil {
		t.Fatalf("package version did not appear in the pkgfs package tree: %s", err)
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

func TestListRoot(t *testing.T) {
	names, err := filepath.Glob(filepath.Join(pkgfsMount, "*"))
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"incoming", "needs", "packages", "metadata"}
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
