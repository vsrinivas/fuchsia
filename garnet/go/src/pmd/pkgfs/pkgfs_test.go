// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package pkgfs

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"
	"testing"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/build"
)

// Adding a file to /in writes the file to blobfs
// Adding a file that is a meta.far to /in creates the package in the package filesystem
// If not all of a packages contents are available, opening the package directory should fail
// A package directory should contain all files from meta.far and listed by meta/contents

var (
	pkgfsDir   fdio.Directory
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

	fmt.Printf("blobfs mounted at %s\n", blobfsPath)

	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.TestPackage(cfg)

	err = build.Update(cfg)
	if err != nil {
		panic(err)
	}
	err = build.Sign(cfg)
	if err != nil {
		panic(err)
	}
	_, err = build.Seal(cfg)
	if err != nil {
		panic(err)
	}

	src, err := os.Open(filepath.Join(cfg.OutputDir, "meta.far"))
	if err != nil {
		panic(err)
	}

	var tree merkle.Tree

	_, err = tree.ReadFrom(src)
	if err != nil {
		panic(err)
	}
	merkleroot := fmt.Sprintf("%x", tree.Root())

	src.Seek(0, os.SEEK_SET)

	f, err := os.Create(filepath.Join(blobfsPath, merkleroot))
	if err != nil {
		panic(err)
	}
	fi, err := src.Stat()
	if err != nil {
		panic(err)
	}
	if err := f.Truncate(int64(fi.Size())); err != nil {
		panic(err)
	}
	if _, err := io.Copy(f, src); err != nil {
		panic(err)
	}
	if err := f.Close(); err != nil {
		panic(err)
	}

	staticFile, err := ioutil.TempFile("", "pkgfs-test-static-index")
	if err != nil {
		panic(err)
	}
	fmt.Fprintf(staticFile, "static-package/0=%s\n", merkleroot)
	staticFile.Close()
	staticPath := staticFile.Name()
	defer os.RemoveAll(staticPath)

	indexPath, err := ioutil.TempDir("", "pkgfs-test-index")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(indexPath)

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

	nc, sc, err := zx.NewChannel(0)
	if err != nil {
		panic(err)
	}

	pkgfsDir = fdio.Directory{fdio.Node{(*zxio.NodeInterface)(&fidl.ChannelProxy{nc})}}

	go func() {
		if err := pkgfs.Serve(sc); err != nil {
			panic(err)
		}
	}()

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

	f, err := pkgfsOpen(filepath.Join("install/pkg", merkleroot), zxio.OpenRightWritable|zxio.OpenFlagCreate, zxio.ModeTypeFile)
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

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): extract into constant in testutil
	packageName := "testpackage"
	packageVersion := "0"

	f, err = pkgfsOpen(filepath.Join("packages", packageName, packageVersion), zxio.OpenRightReadable, zxio.ModeTypeFile)
	if err == nil {
		f.Close()
		t.Error("package appeared in the pkgfs package tree before needs fulfilled")
	}

	f, err = pkgfsOpen(filepath.Join("needs", "blobs"), zxio.OpenRightReadable, zxio.ModeTypeDirectory)
	if err != nil {
		t.Fatal(err)
	}

	needs, err := f.Readdirnames(256)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}

	for i := range needs {
		needs[i] = filepath.Base(needs[i])
	}
	sort.Strings(needs)

	f, err = pkgfsOpen(filepath.Join("needs", "packages", merkleroot), zxio.OpenRightReadable, zxio.ModeTypeDirectory)

	needs2, err := f.Readdirnames(256)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}

	for i := range needs2 {
		needs2[i] = filepath.Base(needs2[i])
	}
	sort.Strings(needs2)

	if len(needs) != len(needs2) {
		t.Errorf("expected needs dirs to be the same: %d != %d", len(needs), len(needs2))
	}

	for i, need := range needs {
		if needs2[i] != need {
			t.Errorf("needs from needs/blobs didn't match package needs at %d", i)
		}
	}

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

		f, err = pkgfsOpen(filepath.Join("install", "blob", root), zxio.OpenRightWritable|zxio.OpenFlagCreate, zxio.ModeTypeFile)
		if err != nil {
			t.Fatal(err)
		}
		from, err := os.Open(manifest.Paths[name])
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.Copy(f, from); err != nil {
			t.Fatal(err)
		}
	}

	var info os.FileInfo
	if info, err = pkgfsStat(filepath.Join("packages", packageName)); err != nil {
		t.Fatalf("package did not appear in the pkgfs package tree: %s", err)
	}
	if !info.IsDir() {
		t.Errorf("os.Stat on package directory says it's not a directory")
	}
	if info, err = pkgfsStat(filepath.Join("packages", packageName, packageVersion)); err != nil {
		t.Fatalf("package version did not appear in the pkgfs package tree: %s", err)
	}
	if !info.IsDir() {
		t.Errorf("os.Stat on package version directory says it's not a directory")
	}

	// put the files into needs and expect the pacakage to be live

	for f := range manifest.Content() {
		b, err := pkgfsReadFile(filepath.Join("packages", packageName, packageVersion, f))
		if err != nil {
			t.Fatal(err)
		}
		if got, want := string(b), f+"\n"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}

	// assert that the dynamically added package appears in /versions
	contents2, err := pkgfsReadFile(filepath.Join("versions", merkleroot, "meta", "contents"))
	if err != nil {
		t.Fatal(err)
	}
	if got, want := string(contents2), string(contents); got != want {
		t.Errorf("add dynamic package, bad version: got %q, want %q", got, want)
	}
}

func pkgfsOpen(path string, flags, mode uint32) (*os.File, error) {
	f, err := pkgfsDir.Open(path, flags, mode)
	if err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(syscall.OpenFDIO(f)), path), err
}

func pkgfsReadFile(path string) ([]byte, error) {
	f, err := pkgfsOpen(path, zxio.OpenRightReadable, zxio.ModeTypeFile)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	buf := bytes.Buffer{}
	if _, err := io.Copy(&buf, f); err != io.EOF && err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func pkgfsStat(path string) (os.FileInfo, error) {
	f, err := pkgfsOpen(path, zxio.OpenRightReadable, zxio.ModeTypeFile|zxio.ModeTypeDirectory)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return f.Stat()
}

func TestListContainsStatic(t *testing.T) {
	//names, err := filepath.Glob(filepath.Join(pkgfsMount, "packages", "*", "*"))
	f, err := pkgfsOpen("packages/static-package/0", zxio.OpenRightReadable, zxio.ModeTypeDirectory)
	if err != nil {
		t.Fatal(err)
	}
	names, err := f.Readdirnames(-1)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}

	if len(names) <= 0 {
		t.Errorf("static-package appears to be empty or missing")
	}
}

func TestListRoot(t *testing.T) {
	f, err := pkgfsOpen(".", zxio.OpenRightReadable, zxio.ModeTypeDirectory)
	if err != nil {
		t.Fatal(err)
	}
	names, err := f.Readdirnames(-1)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"garbage", "install", "needs", "packages", "system", "versions", "validation"}
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

func TestVersions(t *testing.T) {
	f, err := pkgfsOpen("versions", zxio.OpenRightReadable, zxio.ModeTypeDirectory)
	if err != nil {
		t.Fatal(err)
	}
	names, err := f.Readdirnames(-1)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}
	if len(names) == 0 {
		t.Fatal("observed no versions")
	}

	for _, name := range names {
		if !merklePat.MatchString(filepath.Base(name)) {
			t.Fatalf("got non-merkle version: %s", name)
		}

		b, err := pkgfsReadFile(filepath.Join("versions", name, "meta"))
		if err != nil {
			t.Fatal(err)
		}
		if got, want := string(b), filepath.Base(name); got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}
