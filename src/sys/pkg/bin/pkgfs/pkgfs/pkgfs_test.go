// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia
// +build !build_with_native_toolchain

package pkgfs

import (
	"bytes"
	"context"
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
	"testing"

	zxio "syscall/zx/io"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pkgfs/iou"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pkgfs/ramdisk"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
)

// Adding a file to /in writes the file to blobfs
// Adding a file that is a meta.far to /in creates the package in the package filesystem
// If not all of a packages contents are available, opening the package directory should fail
// A package directory should contain all files from meta.far and listed by meta/contents

var (
	pkgfsDir *fdio.Directory
	blobDir  *ramdisk.Ramdisk
)

func installTestPackage(installJustMetaFar bool) string {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.TestPackage(cfg)
	build.BuildTestPackage(cfg)

	bi, err := cfg.BlobInfo()
	if err != nil {
		panic(fmt.Errorf("Creating BlobInfo: %s", err))
	}

	// Install the blobs to blobfs directly.
	for _, b := range bi {
		src, err := os.Open(b.SourcePath)
		if err != nil {
			panic(err)
		}
		dst, err := blobDir.Open(b.Merkle.String(), os.O_WRONLY|os.O_CREATE, 0o700)
		if err != nil {
			panic(fmt.Errorf("Opening blob dst: %s", err))
		}
		if err := dst.Truncate(int64(b.Size)); err != nil {
			panic(err)
		}
		if _, err = io.Copy(dst, src); err != nil {
			panic(err)
		}
		if err := src.Close(); err != nil {
			panic(err)
		}
		if err := dst.Close(); err != nil {
			panic(err)
		}
		if installJustMetaFar {
			return bi[0].Merkle.String()
		}
	}

	return bi[0].Merkle.String()
}

var testPackageMerkle string

// tmain exists for the defer convenience, so that defers are run before os.Exit gets called.
func tmain(m *testing.M) int {
	// Undo the defaults that print to the system log...
	log.SetOutput(os.Stdout)

	var err error
	if blobDir, err = ramdisk.New(10 * 1024 * 1024); err != nil {
		panic(fmt.Errorf("Creating blobfs ramdisk: %s", err))
	}
	if err := blobDir.StartBlobfs(); err != nil {
		panic(fmt.Errorf("Starting blobfs: %s", err))
	}
	defer blobDir.Destroy()

	testPackageMerkle = installTestPackage(false)
	systemImageMerkle := installTestPackage(true)

	d, err := ioutil.TempDir("", "pkgfs-test-mount")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(d)

	blobd, err := blobDir.Open(".", os.O_RDWR|syscall.O_DIRECTORY, 0o700)
	if err != nil {
		panic(err)
	}
	defer func() {
		// The Go syscall API doesn't provide any way to detatch the underlying
		// channel from the *File wrapper, so once the GC runs, then blobd will be
		// closed and then pkgfs can't access the blobfs anymore, so we have to keep
		// it alive for at least the runtime of the tests.
		runtime.KeepAlive(blobd)
	}()

	pkgfs, err := New(syscall.FDIOForFD(int(blobd.Fd())).(*fdio.Directory), false, false)
	if err != nil {
		panic(err)
	}
	systemImagePackage := pkg.Package{
		Name:    "system_image",
		Version: "0",
	}
	pkgfs.static.LoadFrom(strings.NewReader(
		fmt.Sprintf("static-package/0=%s\n", testPackageMerkle)), systemImagePackage, systemImageMerkle)

	nc, sc, err := zx.NewChannel(0)
	if err != nil {
		panic(err)
	}

	pkgfsDir = fdio.NewDirectoryWithCtx(&zxio.DirectoryAdminWithCtxInterface{Channel: nc})
	if err = pkgfs.Serve(sc); err != nil {
		panic(err)
	}
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
	if err != nil {
		t.Fatal(err)
	}
	merkleroot := bi[0].Merkle.String()

	dst, err := iou.OpenFrom(pkgfsDir, filepath.Join("install/pkg", merkleroot), os.O_RDWR|os.O_CREATE, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	if err := dst.Truncate(int64(bi[0].Size)); err != nil {
		t.Fatal(err)
	}
	src, err := os.Open(bi[0].SourcePath)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := io.Copy(dst, src); err != nil {
		src.Close()
		dst.Close()
		t.Fatal(err)
	}
	if src.Close(); err != nil {
		t.Fatal(err)
	}
	if dst.Close(); err != nil {
		t.Fatal(err)
	}

	// Opening it again gives EEXIST
	_, err = iou.OpenFrom(pkgfsDir, filepath.Join("install/pkg", merkleroot), os.O_RDWR|os.O_CREATE, 0o700)
	if !os.IsExist(err) {
		t.Fatal(err)
	}

	d, err := blobDir.Open(merkleroot, syscall.O_PATH, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	_, err = d.Stat()
	d.Close()
	if err != nil {
		t.Fatalf("package blob missing after package write: %s", err)
	}

	f, err := iou.OpenFrom(pkgfsDir, filepath.Join("packages", cfg.PkgName, cfg.PkgVersion), os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
	if err == nil {
		f.Close()
		t.Error("package appeared in the pkgfs package tree before needs fulfilled")
	}

	expectedNeeds := []string{}
	for _, b := range bi {
		if _, err := blobDir.Open(b.Merkle.String(), syscall.O_PATH, 0o700); os.IsNotExist(err) {
			expectedNeeds = append(expectedNeeds, b.Merkle.String())
		}
	}
	sort.Strings(expectedNeeds)

	f, err = iou.OpenFrom(pkgfsDir, filepath.Join("needs", "packages", merkleroot), os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
	needsPkgs, err := f.Readdirnames(256)
	if err != nil {
		t.Error(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}

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

		dst, err := iou.OpenFrom(pkgfsDir, filepath.Join("install/blob", root), os.O_RDWR|os.O_CREATE, 0o700)
		if os.IsExist(err) {
			continue
		}
		if err != nil {
			t.Fatal(err)
		}
		if err := dst.Truncate(int64(b.Size)); err != nil {
			t.Fatal(err)
		}
		src, err := os.Open(b.SourcePath)
		if err != nil {
			t.Fatal(err)
		}
		if _, err = io.Copy(dst, src); err != nil {
			t.Fatal(err)
		}
		if src.Close(); err != nil {
			t.Fatal(err)
		}
		if dst.Close(); err != nil {
			t.Fatal(err)
		}
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
		if err != nil {
			t.Fatal(err)
		}
		want, err := ioutil.ReadFile(b.SourcePath)
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(got, want) {
			t.Errorf("got %x, want %x", got, want)
		}
	}

	// assert that the dynamically added package appears in /versions
	metaMerkle, err := pkgfsReadFile(filepath.Join("versions", merkleroot, "meta"))
	if err != nil {
		t.Fatal(err)
	}
	if got, want := string(metaMerkle), merkleroot; got != want {
		t.Errorf("add dynamic package, bad version: got %q, want %q", got, want)
	}
}

func pkgfsReadFile(path string) ([]byte, error) {
	f, err := iou.OpenFrom(pkgfsDir, path, os.O_RDONLY, 0o700)
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
	f, err := iou.OpenFrom(pkgfsDir, path, os.O_RDONLY|syscall.O_PATH, 0o700)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return f.Stat()
}

func TestMetaFarRootDuality(t *testing.T) {
	path := "packages/static-package/0/meta"

	t.Run("meta is a file containing the merkleroot", func(t *testing.T) {
		f, err := iou.OpenFrom(pkgfsDir, path, 0, 0o700)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		b, err := ioutil.ReadAll(f)
		if err != nil {
			t.Fatal(err)
		}
		if len(b) != 64 && string(b) == testPackageMerkle {
			t.Fatalf("expected 64 byte merkleroot of %q, got %q", testPackageMerkle, string(b))
		}
	})

	t.Run("meta is a directory containing files", func(t *testing.T) {
		f, err := iou.OpenFrom(pkgfsDir, path, syscall.O_DIRECTORY, 0o700)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		list, err := f.Readdirnames(-1)
		if err != nil {
			t.Fatal(err)
		}
		found := false
		for _, item := range list {
			if item == "contents" {
				found = true
				break
			}
		}
		if !found {
			t.Fatalf("did not find 'contents' file among meta/ readdir: %v", list)
		}

		contents, err := iou.OpenFrom(pkgfsDir, filepath.Join(path, "contents"), 0, 0o700)
		if err != nil {
			t.Fatal(err)
		}
		defer contents.Close()
		fi, err := contents.Stat()
		if err != nil {
			t.Fatal(err)
		}

		offset, err := contents.Seek(17, io.SeekStart)
		if err != nil {
			t.Fatal(err)
		}
		if offset != 17 {
			t.Fatalf("Tried to seek to 17 but got %d", offset)
		}
		offset, err = contents.Seek(-7, io.SeekCurrent)
		if err != nil {
			t.Fatal(err)
		}
		if offset != 10 {
			t.Fatalf("Tried to seek to 17-7 but got %d", offset)
		}
		offset, err = contents.Seek(0, io.SeekEnd)
		if err != nil {
			t.Fatal(err)
		}
		if offset == 0 {
			t.Fatalf("Tried to seek to end but got %d", offset)
		}
		if offset != fi.Size() {
			t.Fatalf("Seek to end arrived at %d but expected %d size", offset, fi.Size())
		}
	})

	t.Run("meta subdirectories are openable and listable", func(t *testing.T) {
		f, err := iou.OpenFrom(pkgfsDir, "packages/static-package/0/meta/foo", 0, 0o700)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		fi, err := f.Stat()
		if err != nil {
			t.Fatal(err)
		}
		if !fi.IsDir() {
			t.Fatal("expected static-package/0/meta/foo to be a directory, not a file")
		}
		list, err := f.Readdirnames(-1)
		if err != nil {
			t.Fatal(err)
		}
		if len(list) != 1 && list[0] == "one" {
			t.Fatalf("expected list to contain one file, got %v", list)
		}
	})

	t.Run("meta subdirectories do not have file/directory duality", func(t *testing.T) {
		// protect against regression of name prefix fixup in metafar.go,
		// wherein at time of test authorship, a "." open would open meta/
		// instead.
		d, err := pkgfsDir.Open("packages/static-package/0/meta/foo", syscall.O_DIRECTORY, 0o700)
		if err != nil {
			t.Fatal(err)
		}
		defer d.Close()

		f, err := iou.OpenFrom(d.(*fdio.Directory), "", syscall.O_RDONLY, 0o700)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		fi, err := f.Stat()
		if err != nil {
			t.Fatal(err)
		}
		if !fi.IsDir() {
			t.Fatal("expected static-package/0/meta/foo to be a directory, not a file")
		}
	})

}

func TestExecutability(t *testing.T) {
	// packages/static-package/0/meta/contents should not be openable
	// executable, because meta/* is never executable
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable|syscall.FsRightExecutable, 0o700)
	if f != nil || err == nil {
		t.Fatal(err)
	}

	// packages/static-package/0/a should be openable executable, because
	// files from packages are executable.
	path = "packages/static-package/0/a"
	f, err = pkgfsDir.Open(path, syscall.FsRightReadable|syscall.FsRightExecutable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
}

func TestListContainsStatic(t *testing.T) {
	//names, err := filepath.Glob(filepath.Join(pkgfsMount, "packages", "*", "*"))
	f, err := iou.OpenFrom(pkgfsDir, "packages/static-package/0", os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
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
	f, err := iou.OpenFrom(pkgfsDir, ".", os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
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
	f, err := iou.OpenFrom(pkgfsDir, "ctl", os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
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

func TestSync(t *testing.T) {
	d, err := iou.OpenFrom(pkgfsDir, "ctl", os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	if err = d.Sync(); err != nil {
		t.Fatal(err)
	}
	d.Close()
}

func TestMetaFileGetFlags(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}

	ioFile := (*zxio.FileWithCtxInterface)(fdioFile.Node.NodeWithCtxInterface)

	status, flags, err := ioFile.GetFlags(context.Background())
	if err != nil || status != int32(zx.ErrOk) {
		t.Fatal("Could not get flags:", err, status)
	}

	if flags != syscall.FsRightReadable {
		t.Fatalf("got %v, want %v", flags, syscall.FsRightReadable)
	}
}

func TestMapFileForRead(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}
	defer fdioFile.Close()

	flags := zxio.VmoFlagRead
	status, buffer, err := fdioFile.GetBuffer(flags)
	if err != nil || status != int32(zx.ErrOk) {
		t.Fatal("Could not get buffer:", err, status)
	}
	if buffer.Size == 0 {
		t.Fatal("Buffer has zero size")
	}

	size := buffer.Size
	buf := make([]byte, size)
	offset := uint64(0)
	err = buffer.Vmo.Read(buf, offset)
	if err != nil {
		t.Fatal("Error reading data from VMO")
	}
	buffer.Vmo.Close()
}

func getKoid(h *zx.Handle) (uint64, error) {
	info, err := h.GetInfoHandleBasic()
	if err != nil {
		return 0, err
	}
	return info.Koid, nil
}

func TestMapFileForReadPrivate(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}
	defer fdioFile.Close()

	flags := zxio.VmoFlagRead | zxio.VmoFlagPrivate

	// We want to test that we're receiving our own clone each time we invoke
	// GetBuffer() with the VmoFlagPrivate field set
	status, buffer, err := fdioFile.GetBuffer(flags)
	if err != nil || status != int32(zx.ErrOk) {
		t.Fatal("Could not get buffer:", err, status)
	}

	firstVmo := buffer.Vmo
	defer firstVmo.Close()

	status, buffer, err = fdioFile.GetBuffer(flags)

	if err != nil || status != int32(zx.ErrOk) {
		t.Fatal("Could not get buffer:", err, status)
	}

	secondVmo := buffer.Vmo
	defer secondVmo.Close()

	firstKoid, err := getKoid(firstVmo.Handle())
	if err != nil {
		t.Fatal("Could not retrieve koid of handle: ", err)
	}
	secondKoid, err := getKoid(secondVmo.Handle())
	if err != nil {
		t.Fatal("Could not retrieve koid of handle: ", err)
	}
	if firstKoid == secondKoid {
		t.Fatal("Two GetBuffer calls with VmoFlagPrivate produced handles to the same object")
	}
}

func TestMapFileForReadExact(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}

	// Exact flag is not supported in pkgfs
	flags := zxio.VmoFlagExact

	_, _, err = fdioFile.GetBuffer(flags)
	if err == nil {
		t.Fatal("Attempt to map with VmoFlagExact should fail")
	}
}

func TestMapFilePrivateAndExact(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}

	// This combination is invalid according to the fuchsia.io protocol definition.
	flags := zxio.VmoFlagPrivate | zxio.VmoFlagExact

	_, _, err = fdioFile.GetBuffer(flags)
	if err == nil {
		t.Fatal("Attempt to specify VmoFlagPrivate and VmoFlagExact should fail")
	}
}

func TestMapFileForWrite(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}

	// Files in a meta directory are read-only, creating a writable mapping
	// should fail.
	flags := zxio.VmoFlagWrite
	_, _, err = fdioFile.GetBuffer(flags)
	if err == nil {
		t.Fatal("Attempt to get a writable buffer should fail")
	}
}

func TestMapFileForExec(t *testing.T) {
	path := "packages/static-package/0/meta/contents"
	f, err := pkgfsDir.Open(path, syscall.FsRightReadable, 0o700)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	fdioFile, ok := f.(*fdio.File)
	if !ok {
		t.Fatal("File is not an fdio.File")
	}

	flags := zxio.VmoFlagExec
	_, _, err = fdioFile.GetBuffer(flags)
	if err == nil {
		t.Fatal("Attempt to get executable buffer should fail")
	}
}

func TestTriggerGC(t *testing.T) {
	// always perform the operation on a dedicated channel, so that pkgfsDir is not
	// closed.
	unlink := func(path string) error {
		d, err := pkgfsDir.Open(".", zxio.OpenFlagDirectory|zxio.OpenRightReadable|zxio.OpenFlagPosix, 0o700)
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
	f, err := iou.OpenFrom(pkgfsDir, "versions", os.O_RDONLY|syscall.O_DIRECTORY, 0o700)
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
