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
	"runtime"
	"sort"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"
	"testing"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pmd/iou"
	"fuchsia.googlesource.com/pmd/ramdisk"
)

// Adding a file to /in writes the file to blobfs
// Adding a file that is a meta.far to /in creates the package in the package filesystem
// If not all of a packages contents are available, opening the package directory should fail
// A package directory should contain all files from meta.far and listed by meta/contents

var (
	pkgfsDir *fdio.Directory
	blobDir  *ramdisk.Ramdisk
)

// tmain exists for the defer convenience, so that defers are run before os.Exit gets called.
func tmain(m *testing.M) int {
	// Undo the defaults that print to the system log...
	log.SetOutput(os.Stdout)

	var err error
	blobDir, err = ramdisk.New(10 * 1024 * 1024)
	panicerr(err)
	panicerr(blobDir.StartBlobfs())
	defer blobDir.Destroy()

	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.TestPackage(cfg)
	build.BuildTestPackage(cfg)

	bi, err := cfg.BlobInfo()
	panicerr(err)

	// Install the blobs to blobfs directly
	for _, b := range bi {
		src, err := os.Open(b.SourcePath)
		panicerr(err)
		dst, err := blobDir.Open(b.Merkle.String(), os.O_WRONLY|os.O_CREATE, 0777)
		panicerr(err)
		panicerr(dst.Truncate(int64(b.Size)))
		_, err = io.Copy(dst, src)
		panicerr(err)
		panicerr(src.Close())
		panicerr(dst.Close())
	}

	d, err := ioutil.TempDir("", "pkgfs-test-mount")
	panicerr(err)
	defer os.RemoveAll(d)

	blobd, err := blobDir.Open(".", os.O_RDWR|syscall.O_DIRECTORY, 0777)
	panicerr(err)
	defer func() {
		// The Go syscall API doesn't provide any way to detatch the underlying
		// channel from the *File wrapper, so once the GC runs, then blobd will be
		// closed and then pkgfs can't access the blobfs anymore, so we have to keep
		// it alive for at least the runtime of the tests.
		runtime.KeepAlive(blobd)
	}()

	pkgfs, err := New(syscall.FDIOForFD(int(blobd.Fd())).(*fdio.Directory))
	panicerr(err)
	pkgfs.static.LoadFrom(strings.NewReader(
		fmt.Sprintf("static-package/0=%s\n", bi[0].Merkle.String())))

	nc, sc, err := zx.NewChannel(0)
	panicerr(err)

	pkgfsDir = &fdio.Directory{fdio.Node{(*zxio.NodeInterface)(&fidl.ChannelProxy{nc})}}

	go func() {
		panicerr(pkgfs.Serve(sc))
	}()
	return m.Run()
}

func TestMain(m *testing.M) {
	os.Exit(tmain(m))
}

func TestAddPackage(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	cfg.PkgName = t.Name()

	build.BuildTestPackage(cfg)

	bi, err := cfg.BlobInfo()
	panicerr(err)
	merkleroot := bi[0].Merkle.String()

	dst, err := iou.OpenFrom(pkgfsDir, filepath.Join("install/pkg", merkleroot), os.O_RDWR|os.O_CREATE, 0777)
	panicerr(err)
	panicerr(dst.Truncate(int64(bi[0].Size)))
	src, err := os.Open(bi[0].SourcePath)
	panicerr(err)
	if _, err := io.Copy(dst, src); err != nil {
		src.Close()
		dst.Close()
		t.Fatal(err)
	}
	panicerr(src.Close())
	panicerr(dst.Close())

	// Opening it again gives EEXIST
	_, err = iou.OpenFrom(pkgfsDir, filepath.Join("install/pkg", merkleroot), os.O_RDWR|os.O_CREATE, 0777)
	if !os.IsExist(err) {
		panicerr(err)
	}

	d, err := blobDir.Open(merkleroot, syscall.O_PATH, 0777)
	panicerr(err)
	_, err = d.Stat()
	d.Close()
	if err != nil {
		t.Fatalf("package blob missing after package write: %s", err)
	}

	f, err := iou.OpenFrom(pkgfsDir, filepath.Join("packages", cfg.PkgName, cfg.PkgVersion), os.O_RDONLY|syscall.O_DIRECTORY, 0777)
	if err == nil {
		f.Close()
		t.Error("package appeared in the pkgfs package tree before needs fulfilled")
	}

	expectedNeeds := []string{}
	for _, b := range bi {
		if _, err := blobDir.Open(b.Merkle.String(), syscall.O_PATH, 0777); os.IsNotExist(err) {
			expectedNeeds = append(expectedNeeds, b.Merkle.String())
		}
	}
	sort.Strings(expectedNeeds)

	f, err = iou.OpenFrom(pkgfsDir, filepath.Join("needs", "packages", merkleroot), os.O_RDONLY|syscall.O_DIRECTORY, 0777)
	needsPkgs, err := f.Readdirnames(256)
	panicerr(f.Close())
	panicerr(err)

	if got, want := len(needsPkgs), len(expectedNeeds); got != want {
		t.Errorf("needs/packages/{root}/* count: got %d, want %d", got, want)
	}
	sort.Strings(needsPkgs)
	for i := range expectedNeeds {
		if got, want := filepath.Base(needsPkgs[i]), expectedNeeds[i]; got != want {
			t.Errorf("needs/packages/{root}/{file} got %q, want %q", got, want)
		}
	}

	// install the blobs of the package
	for _, b := range bi[1:] {
		root := b.Merkle.String()
		idx := sort.SearchStrings(needsPkgs, root)
		if idx == len(needsPkgs) {
			continue
		}

		dst, err := iou.OpenFrom(pkgfsDir, filepath.Join("install/blob", root), os.O_RDWR|os.O_CREATE, 0777)
		if os.IsExist(err) {
			continue
		}
		panicerr(err)
		panicerr(dst.Truncate(int64(b.Size)))
		src, err := os.Open(b.SourcePath)
		panicerr(err)
		_, err = io.Copy(dst, src)
		panicerr(err)
		panicerr(src.Close())
		panicerr(dst.Close())
	}

	var info os.FileInfo
	if info, err = pkgfsStat(filepath.Join("packages", cfg.PkgName)); err != nil {
		t.Fatalf("package did not appear in the pkgfs package tree: %s", err)
	}
	if !info.IsDir() {
		t.Errorf("os.Stat on package directory says it's not a directory")
	}
	if info, err = pkgfsStat(filepath.Join("packages", cfg.PkgName, cfg.PkgVersion)); err != nil {
		t.Fatalf("package version did not appear in the pkgfs package tree: %s", err)
	}
	if !info.IsDir() {
		t.Errorf("os.Stat on package version directory says it's not a directory")
	}

	for _, b := range bi[1:] {
		got, err := pkgfsReadFile(filepath.Join("packages", cfg.PkgName, cfg.PkgVersion, b.Path))
		panicerr(err)
		want, err := ioutil.ReadFile(b.SourcePath)
		panicerr(err)
		if !bytes.Equal(got, want) {
			t.Errorf("got %x, want %x", got, want)
		}
	}

	// assert that the dynamically added package appears in /versions
	metaMerkle, err := pkgfsReadFile(filepath.Join("versions", merkleroot, "meta"))
	panicerr(err)
	if got, want := string(metaMerkle), merkleroot; got != want {
		t.Errorf("add dynamic package, bad version: got %q, want %q", got, want)
	}
}

func pkgfsReadFile(path string) ([]byte, error) {
	f, err := iou.OpenFrom(pkgfsDir, path, os.O_RDONLY, 0777)
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
	f, err := iou.OpenFrom(pkgfsDir, path, os.O_RDONLY|syscall.O_PATH, 0777)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return f.Stat()
}

func TestExecutability(t *testing.T) {
	// packages/static-package/0/meta/contents should not be openable
	// executable, because meta/* is never executable
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable|syscall.FsRightExecutable, 0777)
	if f != nil || err == nil {
		t.Fatal(err)
	}

	// packages/static-package/0/a should be openable executable, because
	// files from packages are executable.
	path = "packages/static-package/0/a"
	f, err = pkgfsDir.Open(path, syscall.FsRightReadable|syscall.FsRightExecutable, 0777)
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
}

func TestListContainsStatic(t *testing.T) {
	//names, err := filepath.Glob(filepath.Join(pkgfsMount, "packages", "*", "*"))
	f, err := iou.OpenFrom(pkgfsDir, "packages/static-package/0", os.O_RDONLY|syscall.O_DIRECTORY, 0777)
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
	f, err := iou.OpenFrom(pkgfsDir, ".", os.O_RDONLY|syscall.O_DIRECTORY, 0777)
	if err != nil {
		t.Fatal(err)
	}
	names, err := f.Readdirnames(-1)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"install", "needs", "packages", "system", "versions", "ctl"}
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

func TestListCtl(t *testing.T) {
	f, err := iou.OpenFrom(pkgfsDir, "ctl", os.O_RDONLY|syscall.O_DIRECTORY, 0777)
	if err != nil {
		t.Fatal(err)
	}
	names, err := f.Readdirnames(-1)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"garbage", "validation"}
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

func TestTriggerGC(t *testing.T) {
	// always perform the operation on a dedicated channel, so that pkgfsDir is not
	// closed.
	unlink := func(path string) error {
		d, err := pkgfsDir.Open(".", zxio.OpenFlagDirectory|zxio.OpenRightReadable|zxio.OpenFlagPosix, 0777)
		if err != nil {
			return err
		}
		return d.Unlink(path)
	}

	// /pkgfs/garbage no longer exists
	if err := unlink("garbage"); err == nil {
		t.Fatal("expected error, got nil")
	}

	// unlinking garbage triggers a GC but doesn't remove the file.
	if err := unlink("ctl/garbage"); err != nil {
		t.Fatal(err)
	}
	if err := unlink("ctl/garbage"); err != nil {
		t.Fatal(err)
	}
}

func TestVersions(t *testing.T) {
	f, err := iou.OpenFrom(pkgfsDir, "versions", os.O_RDONLY|syscall.O_DIRECTORY, 0777)
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
			t.Errorf("got non-merkle version: %q", name)
			continue
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

func panicerr(err error) {
	if err != nil {
		panic(err)
	}
}
