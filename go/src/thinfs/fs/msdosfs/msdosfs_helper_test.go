// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package msdosfs

import (
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/block"
	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/testutil"
)

// Functions to set up and shut down FAT filesystems

func setupFAT32(t *testing.T) (*testutil.FileFAT, block.Device) {
	fileBackedFAT := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	dev := fileBackedFAT.GetRawDevice()
	return fileBackedFAT, dev
}

func setupFAT16(t *testing.T) (*testutil.FileFAT, block.Device) {
	fileBackedFAT := testutil.MkfsFAT(t, "10M", 2, 0, 4, 512)
	dev := fileBackedFAT.GetRawDevice()
	return fileBackedFAT, dev
}

func cleanup(fileBackedFAT *testutil.FileFAT, dev block.Device) {
	fileBackedFAT.RmfsFAT()
	dev.Close()
}

func checkNewFS(t *testing.T, dev block.Device, opts fs.FileSystemOptions) fs.FileSystem {
	fatFS, err := New("My FAT Filesystem", dev, opts)
	if err != nil {
		t.Fatal(err)
	}
	return fatFS
}

func checkCloseFS(t *testing.T, fatFS fs.FileSystem) {
	if err := fatFS.Close(); err != nil {
		t.Fatal(err)
	}
}

// Helper functions to test common file operations

// To check that the file / directory exists, simply try opening it.
func checkExists(t *testing.T, d fs.Directory, path string) {
	f, dir, err := d.Open(path, fs.OpenFlagRead)
	if err != nil {
		t.Fatalf("Expected file to exist, but saw error: %v", err)
	}
	if f != nil {
		checkClose(t, f)
	} else if dir != nil {
		checkClose(t, dir)
	}
}

// To check that the file / directory does not exist, try opening it as readable.
// If it DOES exist, a file or directory would be opened.
func checkDoesNotExist(t *testing.T, d fs.Directory, path string) {
	if _, _, err := d.Open(path, fs.OpenFlagRead); err != fs.ErrNotFound {
		t.Fatal("Expected ErrNotFound, saw: ", err)
	}
}

// Helper to validate that directory has unlinked child
func checkDirectoryEmpty(t *testing.T, d fs.Directory) {
	contents := checkReadDir(t, d, 1)
	checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
}

// Helper to validate that directory has not yet unlinked a child
// 'length' refers to the exptected number of entries from readdir.
func checkDirectoryContains(t *testing.T, d fs.Directory, name string, fType fs.FileType, length int) {
	contents := checkReadDir(t, d, length)
	checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
	found := false
	for i := 1; i < length; i++ {
		if contents[i].GetName() == name {
			if found {
				t.Fatalf("Dirent named: %s was found twice", name)
			}
			found = true
			checkDirent(t, contents[i], name, fType)
		}
	}
	if !found {
		t.Fatalf("Dirent named: %s was not contained in the parent directory", name)
	}
}

// Helper to validate the contents of a dirent.
func checkDirent(t *testing.T, dirent fs.Dirent, name string, fType fs.FileType) {
	if dirent.GetType() != fType {
		t.Fatalf("Unexpected dirent type: %d; expected %s to have type %d\n", dirent.GetType(), name, fType)
	} else if dirent.GetName() != name {
		t.Fatalf("Unexpected dirent name: %s; expected %s\n", dirent.GetName(), name)
	}
}

// Checked file / directory operations to make the success case cleaner

func checkOpen(t *testing.T, d fs.Directory, name string, flags fs.OpenFlags) interface{} {
	f, dir, err := d.Open(name, flags)
	if err != nil {
		t.Fatal(err)
	}

	if f != nil {
		return f
	}
	return dir
}

func checkClose(t *testing.T, n interface{}) {
	if f, ok := n.(fs.File); ok {
		checkCloseFile(t, f)
	} else if d, ok := n.(fs.Directory); ok {
		checkCloseDir(t, d)
	} else if fs, ok := n.(fs.FileSystem); ok {
		checkCloseFS(t, fs)
	} else {
		t.Fatal("Unexpected type")
	}
}

func checkStat(t *testing.T, n interface{}) (int64, time.Time, time.Time) {
	if f, ok := n.(fs.File); ok {
		return checkStatFile(t, f)
	} else if d, ok := n.(fs.Directory); ok {
		return checkStatDir(t, d)
	}
	t.Fatal("Unexpected type")
	return 0, time.Time{}, time.Time{}
}

func checkTouch(t *testing.T, n interface{}, access, modified time.Time) {
	if f, ok := n.(fs.File); ok {
		checkTouchFile(t, f, access, modified)
	} else if d, ok := n.(fs.Directory); ok {
		checkTouchDir(t, d, access, modified)
	} else {
		t.Fatal("Unexpected type")
	}
}

// Checked directory operations to make the success case cleaner

func checkCloseDir(t *testing.T, d fs.Directory) {
	if err := d.Close(); err != nil {
		t.Fatal(err)
	}
}

func checkStatDir(t *testing.T, d fs.Directory) (int64, time.Time, time.Time) {
	size, atime, mtime, err := d.Stat()
	if err != nil {
		t.Fatal(err)
	}
	return size, atime, mtime
}

func checkTouchDir(t *testing.T, d fs.Directory, lastAccess, lastModified time.Time) {
	if err := d.Touch(lastAccess, lastModified); err != nil {
		t.Fatal(err)
	}
}

func checkDupDir(t *testing.T, d fs.Directory) fs.Directory {
	newDir, err := d.Dup()
	if err != nil {
		t.Fatal(err)
	}
	return newDir
}

func checkReopenDir(t *testing.T, d fs.Directory, flags fs.OpenFlags) fs.Directory {
	newDir, err := d.Reopen(flags)
	if err != nil {
		t.Fatal(err)
	}
	return newDir
}

func checkReadDir(t *testing.T, d fs.Directory, length int) []fs.Dirent {
	contents, err := d.Read()
	if err != nil {
		t.Fatal(err)
	} else if len(contents) != length {
		t.Fatalf("Unexpected number of directory entries: %d (expected %d)\n", len(contents), length)
	}
	return contents
}

func checkOpenFile(t *testing.T, d fs.Directory, name string, flags fs.OpenFlags) fs.File {
	f, dir, err := d.Open(name, flags|fs.OpenFlagFile)
	if err != nil {
		t.Fatal(err)
	} else if f == nil || dir != nil {
		t.Fatal("Expected 'Open' to open a file and not a directory")
	}
	return f
}

func checkOpenDirectory(t *testing.T, d fs.Directory, name string, flags fs.OpenFlags) fs.Directory {
	f, newDir, err := d.Open(name, flags|fs.OpenFlagDirectory)
	if err != nil {
		t.Fatal(err)
	} else if newDir == nil || f != nil {
		t.Fatal("Expected 'Open' to open a directory and not a file")
	}
	return newDir
}

func checkRename(t *testing.T, d fs.Directory, src, dst string) {
	if err := d.Rename(d, src, dst); err != nil {
		t.Fatal(err)
	}
}

func checkSync(t *testing.T, d fs.Directory) {
	if err := d.Sync(); err != nil {
		t.Fatal(err)
	}
}

func checkUnlink(t *testing.T, d fs.Directory, target string) {
	if err := d.Unlink(target); err != nil {
		t.Fatal(err)
	}
}

// Checked file operations to make the success case cleaner

func checkCloseFile(t *testing.T, f fs.File) {
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
}

func checkStatFile(t *testing.T, f fs.File) (int64, time.Time, time.Time) {
	size, atime, mtime, err := f.Stat()
	if err != nil {
		t.Fatal(err)
	}
	return size, atime, mtime
}

func checkTouchFile(t *testing.T, f fs.File, lastAccess, lastModified time.Time) {
	if err := f.Touch(lastAccess, lastModified); err != nil {
		t.Fatal(err)
	}
}

func checkDupFile(t *testing.T, f fs.File) fs.File {
	newFile, err := f.Dup()
	if err != nil {
		t.Fatal(err)
	}
	return newFile
}

func checkReopenFile(t *testing.T, f fs.File, flags fs.OpenFlags) fs.File {
	newFile, err := f.Reopen(flags)
	if err != nil {
		t.Fatal(err)
	}
	return newFile
}

func checkRead(t *testing.T, f fs.File, p []byte, off int64, whence int) {
	if n, err := f.Read(p, off, whence); err != nil {
		t.Fatal(err)
	} else if n != len(p) {
		t.Fatalf("Read %d bytes (expected %d bytes)", n, len(p))
	}
}

func checkWrite(t *testing.T, f fs.File, p []byte, off int64, whence int) {
	if n, err := f.Write(p, off, whence); err != nil {
		t.Fatal(err)
	} else if n != len(p) {
		t.Fatalf("Wrote %d bytes (expected %d bytes)", n, len(p))
	}
}

func checkTruncate(t *testing.T, f fs.File, size uint64) {
	if err := f.Truncate(size); err != nil {
		t.Fatal(err)
	}
}

func checkTell(t *testing.T, f fs.File) int64 {
	n, err := f.Tell()
	if err != nil {
		t.Fatal(err)
	}
	return n
}

func checkSeek(t *testing.T, f fs.File, offset int64, whence int) int64 {
	n, err := f.Seek(offset, whence)
	if err != nil {
		t.Fatal(err)
	}
	return n
}
