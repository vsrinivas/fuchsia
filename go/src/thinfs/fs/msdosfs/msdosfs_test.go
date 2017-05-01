// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package msdosfs

import (
	"bytes"
	"math/rand"
	"testing"
	"time"

	"github.com/golang/glog"

	"fuchsia.googlesource.com/thinfs/block"
	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/node"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/testutil"
)

// Without accessing any nodes, mount and unmount the filesystem
func TestMountUnmount(t *testing.T) {
	fileBackedFAT, dev := setupFAT32(t)
	defer cleanup(fileBackedFAT, dev)

	fatFS := checkNewFS(t, dev, fs.ReadWrite)

	blockcount := fatFS.Blockcount()
	blocksize := fatFS.Blocksize()
	size := fatFS.Size()

	if blockcount <= 0 {
		t.Fatalf("Expected positive blockcount, but got %d", blockcount)
	} else if blocksize != 2048 {
		t.Fatalf("Expected blocksize of %d, but got %d", 2048, blocksize)
	} else if size != blockcount*blocksize {
		t.Fatalf("Expected size of %d, but got %d", blockcount*blocksize, size)
	}

	root := fatFS.RootDirectory()
	if root == nil {
		t.Fatal("RootDirectory returned nil")
	}

	checkClose(t, root)
	checkCloseFS(t, fatFS)
}

// Test some basic directory / file operations inside the root
func TestRootBasic(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		rootContents := checkReadDir(t, root, 2)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)

		// First, try to open the subdirectory as if it existed.
		if _, _, err := root.Open("subdir", fs.OpenFlagRead); err != fs.ErrNotFound {
			t.Fatalf("Expected ErrNotFound, saw err: %s", err)
		}
		// Okay, it doesn't exist. Try creating it.
		subdir := checkOpenDirectory(t, root, "subdir", fs.OpenFlagWrite|fs.OpenFlagRead|fs.OpenFlagCreate)

		// Verify that the new directory is empty.
		subDirContents := checkReadDir(t, subdir, 2)
		checkDirent(t, subDirContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, subDirContents[1], "..", fs.FileTypeDirectory)

		// Verify the root has been updated.
		rootContents = checkReadDir(t, root, 3)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, rootContents[2], "subdir", fs.FileTypeDirectory)

		checkClose(t, subdir)

		// Try making a file in root.
		if _, _, err := root.Open("foo", fs.OpenFlagRead); err != fs.ErrNotFound {
			t.Fatalf("Expected ErrNotFound, saw err: %s", err)
		}
		foo := checkOpenFile(t, root, "foo", fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate)

		// Verify the root has been updated.
		rootContents = checkReadDir(t, root, 4)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, rootContents[2], "subdir", fs.FileTypeDirectory)
		checkDirent(t, rootContents[3], "foo", fs.FileTypeRegularFile)

		// Write to the file, close it, reopen it, and read it.
		writeBuf := []byte{'a', 'b', 'c'}
		checkWrite(t, foo, writeBuf, 0, fs.WhenceFromStart)
		checkClose(t, foo)
		if _, _, err := root.Open("foo", fs.OpenFlagRead|fs.OpenFlagCreate|fs.OpenFlagExclusive); err != fs.ErrAlreadyExists {
			t.Fatalf("Expected ErrAlreadyExists, saw err: %s", err)
		}
		foo = checkOpenFile(t, root, "foo", fs.OpenFlagRead)
		readBuf := make([]byte, len(writeBuf))
		checkRead(t, foo, readBuf, 0, fs.WhenceFromStart)
		if !bytes.Equal(readBuf, writeBuf) {
			t.Fatal("Input buffer did not equal output buffer")
		}

		// Close the root, unmount the filesystem.
		checkClose(t, root)
		checkCloseFS(t, fatFS)

		// Remount the filesystem, verify everything still exists.
		fatFS = checkNewFS(t, dev, fs.ReadWrite)
		root = fatFS.RootDirectory()

		// Verify root
		rootContents = checkReadDir(t, root, 4)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, rootContents[2], "subdir", fs.FileTypeDirectory)
		checkDirent(t, rootContents[3], "foo", fs.FileTypeRegularFile)

		// Verify subdirectory
		subdir = checkOpenDirectory(t, root, "subdir", fs.OpenFlagRead)
		subDirContents = checkReadDir(t, subdir, 2)
		checkDirent(t, subDirContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, subDirContents[1], "..", fs.FileTypeDirectory)

		// Verify foo
		foo = checkOpenFile(t, root, "foo", fs.OpenFlagRead)
		checkRead(t, foo, readBuf, 0, fs.WhenceFromStart)
		if !bytes.Equal(readBuf, writeBuf) {
			t.Fatal("Input buffer did not equal output buffer")
		}

		checkClose(t, foo)
		checkClose(t, subdir)
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32 Root")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16 Root")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test that the open flags behave as they should
func TestOpenFlags(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		testOpenFlags := func(d fs.Directory) {
			// Start with an empty directory
			contents := checkReadDir(t, d, 2)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)

			// Try creating a node without specifying if it is a file or directory
			if _, _, err := d.Open("foo", fs.OpenFlagCreate|fs.OpenFlagWrite); err != fs.ErrInvalidArgs {
				t.Fatalf("Expected ErrInvalidArgs, saw err: %s", err)
			}

			// Try creating a file without permissions
			if _, _, err := d.Open("foo", fs.OpenFlagCreate|fs.OpenFlagFile); err != fs.ErrPermission {
				t.Fatalf("Expected ErrInvalidArgs, saw err: %s", err)
			}

			// Try creating a file without write permissions
			if _, _, err := d.Open("foo", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagFile); err != fs.ErrPermission {
				t.Fatalf("Expected ErrInvalidArgs, saw err: %s", err)
			}

			// Make a file that is writeable (without append)
			f := checkOpenFile(t, d, "foo", fs.OpenFlagCreate|fs.OpenFlagWrite|fs.OpenFlagFile)

			// Test that the file is writable
			bufRead := make([]byte, 100)
			bufA := testutil.MakeRandomBuffer(100)
			checkWrite(t, f, bufA, 0, fs.WhenceFromCurrent)
			// Test that the file is seekable
			if pos := checkSeek(t, f, 0, fs.WhenceFromStart); pos != 0 {
				t.Fatal("Unexpected seek position: ", pos)
			}
			// Test that the file is not readable
			if _, err := f.Read(bufRead, 0, fs.WhenceFromStart); err != fs.ErrPermission {
				t.Fatal("Expected permission error, saw ", err)
			}
			checkClose(t, f)

			// Re-open the file as readable (without append, and without 'file' flag)
			f = checkOpenFile(t, d, "foo", fs.OpenFlagRead)

			// Test that the file is not writeable
			if _, err := f.Write(bufA, 0, fs.WhenceFromCurrent); err != fs.ErrPermission {
				t.Fatal("Expected permission error, saw ", err)
			}
			// Test that the file is readable
			checkRead(t, f, bufRead, 0, fs.WhenceFromStart)
			if !bytes.Equal(bufRead, bufA) {
				t.Fatal("Bytes written as write-only did not equal bytes read as read-only")
			}
			// Test that the file is seekable
			if pos := checkSeek(t, f, 0, fs.WhenceFromStart); pos != 0 {
				t.Fatal("Unexpected seek position: ", pos)
			}
			checkClose(t, f)

			// Re-open the file as append-only
			f = checkOpenFile(t, d, "foo", fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagAppend)
			// Test that file is still seekable
			checkSeek(t, f, 1, fs.WhenceFromStart)
			sizeBefore, _, _ := checkStat(t, f)
			// Test a command that normally would not append
			if _, err := f.Write(bufA, 0, fs.WhenceFromStart); err != nil {
				t.Fatal(err)
			}
			// Observe that we appended to the file anyway
			sizeAfter, _, _ := checkStat(t, f)
			if sizeBefore+int64(len(bufA)) != sizeAfter {
				t.Fatalf("Expected append-only write to add %d bytes to file", len(bufA))
			}
			checkClose(t, f)

			// Try opening the file as a directory
			var err error
			if _, _, err = d.Open("foo", fs.OpenFlagRead|fs.OpenFlagDirectory); err != fs.ErrNotADir {
				t.Fatalf("Expected ErrNotADir, but saw: %s", err)
			}
			// Try creating the file as a directory (non-exclusive)
			flags := fs.OpenFlagRead | fs.OpenFlagWrite | fs.OpenFlagDirectory | fs.OpenFlagCreate
			if _, _, err = d.Open("foo", flags); err != fs.ErrNotADir {
				t.Fatalf("Expected ErrAlreadyExists, but saw: %s", err)
			}
			// Try creating the file as a directory (exclusive)
			flags |= fs.OpenFlagExclusive
			if _, _, err = d.Open("foo", flags); err != fs.ErrAlreadyExists {
				t.Fatalf("Expected ErrAlreadyExists, but saw: %s", err)
			}
			flags = fs.OpenFlagWrite | fs.OpenFlagDirectory | fs.OpenFlagCreate
			// Try making a new directory without read permissions (note the new name)
			if _, _, err = d.Open("newdir", flags); err != fs.ErrPermission {
				t.Fatalf("Expected error due to lack of read perm, but saw: %s", err)
			}

			// Try truncating a file without write permissions
			if _, _, err = d.Open("foo", fs.OpenFlagRead|fs.OpenFlagTruncate); err != fs.ErrPermission {
				t.Fatalf("Expected ErrPermission, but saw %s", err)
			}
			// Re-open the file, successfully truncating it
			f = checkOpenFile(t, d, "foo", fs.OpenFlagWrite|fs.OpenFlagTruncate)
			size, _, _ := checkStat(t, f)
			if size != 0 {
				t.Fatal("Truncate flag should have set the file size to zero")
			}

			// Verify the size of the truncated file
			size, _, _ = checkStat(t, f)
			if size != int64(0) {
				t.Fatalf("Unexpected size from stat: %d (expected %d)", size, 0)
			}

			checkClose(t, f)

			contents = checkReadDir(t, d, 3)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
			checkDirent(t, contents[2], "foo", fs.FileTypeRegularFile)
		}

		testOpenFlags(root)
		subdir := checkOpenDirectory(t, root, "subdir", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)
		testOpenFlags(subdir)

		checkClose(t, subdir)
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test that readdir still functions when the directory contains "free" direntries
func TestDirectoryHoles(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		// Confirm that the directory contains 'filenames' as files, in order.
		confirmDirectoryContents := func(d fs.Directory, filenames []string) {
			contents := checkReadDir(t, d, len(filenames)+2)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
			for i := range filenames {
				checkDirent(t, contents[i+2], filenames[i], fs.FileTypeRegularFile)
			}
		}

		doTest := func(d fs.Directory) {
			// Start with an empty directory
			contents := checkReadDir(t, d, 2)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)

			filenames := []string{
				"foo",
				"This is a long file name",
				"This is also a long file name, which uses multiple FAT direntries",
				"short",
				"One more long filename, for good measure",
			}

			// Create all files
			for i := range filenames {
				f := checkOpenFile(t, d, filenames[i], fs.OpenFlagCreate|fs.OpenFlagWrite|fs.OpenFlagFile)
				checkClose(t, f)
			}

			// Verify initial state
			confirmDirectoryContents(d, filenames)

			for len(filenames) != 0 {
				// Randomly pick one entry to remove until the directory is empty
				i := rand.Intn(len(filenames))
				checkUnlink(t, d, filenames[i])
				filenames = append(filenames[:i], filenames[i+1:]...)

				// Confirm the directory still contains the rest of the filenames
				confirmDirectoryContents(d, filenames)
			}

			// When we're finished, the directory should be empty
			checkDirectoryEmpty(t, d)
		}

		doTest(root)
		subdir := checkOpenDirectory(t, root, "subdir", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)
		doTest(subdir)

		checkClose(t, subdir)
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test unmounting a filesystem with open files and directories. Verify that the file hierarchy is
// stored on disk when unmount is called while files are still open.
func TestUnmountWithOpenFiles(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		// /aaa/bbb/ccc/ddd.txt
		// 'aaa' will have one ref, 'bbb' will have no refs, 'ccc' will have two refs, 'ddd' will
		// have one ref. Additionally, there is an unlinked file which will not be saved.
		flags := fs.OpenFlagCreate | fs.OpenFlagRead | fs.OpenFlagWrite
		aaa := checkOpenDirectory(t, root, "aaa", flags)
		bbb := checkOpenDirectory(t, aaa, "bbb", flags)
		ccc := checkOpenDirectory(t, bbb, "ccc", flags)
		checkOpenDirectory(t, bbb, "ccc", flags)
		checkClose(t, bbb)
		checkOpenFile(t, ccc, "ddd.txt", flags)

		// Close the filesystem before individually closing any file / directory
		checkCloseFS(t, fatFS)

		// Re-open the filesystem, verify its structure
		fatFS = checkNewFS(t, dev, fs.ReadOnly)
		root = fatFS.RootDirectory()

		contents := checkReadDir(t, root, 3)
		checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, contents[2], "aaa", fs.FileTypeDirectory)

		aaa = checkOpenDirectory(t, root, "aaa", fs.OpenFlagRead)
		contents = checkReadDir(t, aaa, 3)
		checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, contents[2], "bbb", fs.FileTypeDirectory)

		bbb = checkOpenDirectory(t, aaa, "bbb", fs.OpenFlagRead)
		contents = checkReadDir(t, bbb, 3)
		checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, contents[2], "ccc", fs.FileTypeDirectory)

		ccc = checkOpenDirectory(t, bbb, "ccc", fs.OpenFlagRead)
		contents = checkReadDir(t, ccc, 3)
		checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, contents[2], "ddd.txt", fs.FileTypeRegularFile)

		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test the file cursor when reading and writing to a file
func TestFilePositionValid(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		bufA := testutil.MakeRandomBuffer(1000)
		bufB := testutil.MakeRandomBuffer(500)
		readBuf := make([]byte, len(bufA))

		// Start off writing from the start of the file
		f := checkOpenFile(t, root, "foo", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)
		checkWrite(t, f, bufA, 0, fs.WhenceFromCurrent)
		if n := checkTell(t, f); n != int64(len(bufA)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}
		// Read the same buffer to verify the write
		checkRead(t, f, readBuf, 0, fs.WhenceFromStart)
		if !bytes.Equal(bufA, readBuf) {
			t.Fatal("Read buffer did not equal write buffer")
		}

		// The cursor already is at the end of the file.
		// Try writing beyond the end of the file
		extraOffset := int64(200)
		checkWrite(t, f, bufB, extraOffset, fs.WhenceFromCurrent)
		if n := checkTell(t, f); n != int64(len(bufA)+200+len(bufB)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}
		// Try reading the file using a negative offset
		readBuf = make([]byte, len(bufA)+len(bufB)+int(extraOffset))
		checkRead(t, f, readBuf, int64(-len(readBuf)), fs.WhenceFromCurrent)
		if !bytes.Equal(readBuf, append(append(bufA, make([]byte, extraOffset)...), bufB...)) {
			t.Fatal("Read buffer did not equal write buffer")
		}

		// Double check the seek position
		if n := checkTell(t, f); n != int64(len(bufA)+200+len(bufB)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}

		// Truncate the file
		checkTruncate(t, f, uint64(len(bufA)))
		// Observe that the size has changed
		if size, _, _ := checkStat(t, f); size != int64(len(bufA)) {
			t.Fatalf("Unexpected post-truncation size: %d", size)
		}
		// Observe that the seek position has not changed
		if n := checkTell(t, f); n != int64(len(bufA)+200+len(bufB)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}

		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test invalid operations on a file when the seek position is invalid
func TestFilePositionInvalid(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		buf := testutil.MakeRandomBuffer(100)
		readBuf := make([]byte, 1)

		// Start off writing from the start of the file
		f := checkOpenFile(t, root, "foo", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)
		checkWrite(t, f, buf, 0, fs.WhenceFromCurrent)
		if n := checkTell(t, f); n != int64(len(buf)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}

		tryEmptyRead := func(off int64, whence int) {
			n, err := f.Read(readBuf, off, whence)
			if n != 0 || err != fs.ErrEOF {
				t.Fatalf("Expected empty read, saw: %d bytes, err: %s", n, err)
			}
		}
		tryEmptyRead(0, fs.WhenceFromCurrent)             // Read from current position (end of file)
		tryEmptyRead(0, fs.WhenceFromEnd)                 // Read from end of file
		tryEmptyRead(5, fs.WhenceFromEnd)                 // Read from end of file + five bytes
		tryEmptyRead(int64(len(buf)), fs.WhenceFromStart) // Read from start + size of file

		tryBadRead := func(off int64, whence int) {
			n, err := f.Read(readBuf, off, whence)
			if n != 0 || err != node.ErrBadArgument {
				t.Fatalf("Expected bad read, saw: %d bytes, err: %s", n, err)
			}
		}

		tryBadRead(-1, fs.WhenceFromStart)                   // Read from start of file - 1
		tryBadRead(-int64(len(buf)+1), fs.WhenceFromEnd)     // Read from end - (size of file + 1)
		tryBadRead(-int64(len(buf)+1), fs.WhenceFromCurrent) // Read from current (end) - (size of file + 1)

		tryBadWrite := func(off int64, whence int) {
			n, err := f.Write([]byte{'a'}, off, whence)
			if n != 0 || err != node.ErrBadArgument {
				t.Fatalf("Expected bad write, saw: %d bytes, err: %s", n, err)
			}
		}

		tryBadWrite(-1, fs.WhenceFromStart)                   // Write to start - 1
		tryBadWrite(-int64(len(buf)+1), fs.WhenceFromEnd)     // Write to end - (size of file + 1)
		tryBadWrite(-int64(len(buf)+1), fs.WhenceFromCurrent) // Write to current (end) - (size of file + 1)

		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test the effect of duplicating files and directories
func TestDup(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		buf := testutil.MakeRandomBuffer(100)
		// Dup 'f' as 'f2'. Verify the seek position stays the same.
		f := checkOpenFile(t, root, "foo", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)
		f2 := checkDupFile(t, f)
		checkWrite(t, f, buf, 0, fs.WhenceFromCurrent)

		if n := checkTell(t, f); n != int64(len(buf)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}
		if n := checkTell(t, f2); n != int64(len(buf)) {
			t.Fatalf("Unexpected seek position: %d", n)
		}

		// Move the seek position on 'f2', check that 'f' also moves.
		checkSeek(t, f2, -50, fs.WhenceFromCurrent)
		newPosition := int64(50)
		if n := checkTell(t, f); n != newPosition {
			t.Fatalf("Unexpected seek position: %d", n)
		}
		if n := checkTell(t, f2); n != newPosition {
			t.Fatalf("Unexpected seek position: %d", n)
		}
		checkClose(t, f)
		checkClose(t, f2)

		// Try Dup with a readonly file
		f = checkOpenFile(t, root, "foo", fs.OpenFlagRead)
		f2 = checkDupFile(t, f)
		if _, err := f2.Write(buf, 0, fs.WhenceFromEnd); err != fs.ErrPermission {
			t.Fatal("Expected permission error")
		}
		checkClose(t, f)
		checkClose(t, f2)

		// Try Dup on a directory
		d := checkOpenDirectory(t, root, "dir", fs.OpenFlagWrite|fs.OpenFlagRead|fs.OpenFlagCreate)
		d2 := checkDupDir(t, d)

		// Verify that dup worked on a directory by writing to d...
		f = checkOpenFile(t, d, "file", fs.OpenFlagWrite|fs.OpenFlagCreate)
		checkClose(t, f)

		// ... but reading the result on d2.
		rootContents := checkReadDir(t, d2, 3)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, rootContents[2], "file", fs.FileTypeRegularFile)

		checkClose(t, d)
		checkClose(t, d2)
		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test access to a readonly filesystem
func TestReadonly(t *testing.T) {
	doTest := func(dev block.Device) {
		// First, create a filesystem as read-write, so we can create some files and directories.
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		// Create a file in root and write to it
		foo := checkOpenFile(t, root, "foo", fs.OpenFlagWrite|fs.OpenFlagCreate)
		fooContents := "This is the data inside file foo"
		checkWrite(t, foo, []byte(fooContents), 0, fs.WhenceFromStart)
		checkClose(t, foo)

		// Create a subdirectory
		subdir := checkOpenDirectory(t, root, "subdir", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)

		// Create a file in the subdirectory and write to it
		bar := checkOpenFile(t, subdir, "bar", fs.OpenFlagWrite|fs.OpenFlagCreate)
		barContents := "This is different data inside file bar"
		checkWrite(t, bar, []byte(barContents), 0, fs.WhenceFromStart)
		checkClose(t, subdir)
		checkClose(t, bar)

		// Close the filesystem
		checkClose(t, root)
		checkCloseFS(t, fatFS)

		// Reopen the filesystem as readonly
		fatFS = checkNewFS(t, dev, fs.ReadOnly)
		root = fatFS.RootDirectory()

		// Verify the structure of the filesystem
		rootContents := checkReadDir(t, root, 4)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, rootContents[2], "foo", fs.FileTypeRegularFile)
		checkDirent(t, rootContents[3], "subdir", fs.FileTypeDirectory)

		subdir = checkOpenDirectory(t, root, "subdir", fs.OpenFlagRead)
		subdirContents := checkReadDir(t, subdir, 3)
		checkDirent(t, subdirContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, subdirContents[1], "..", fs.FileTypeDirectory)
		checkDirent(t, subdirContents[2], "bar", fs.FileTypeRegularFile)

		// Verify the contents of the files. Verify that new writes fail
		foo = checkOpenFile(t, root, "foo", fs.OpenFlagRead)
		readBuf := make([]byte, len(fooContents))
		checkRead(t, foo, readBuf, 0, fs.WhenceFromStart)
		if !bytes.Equal(readBuf, []byte(fooContents)) {
			t.Fatal("File foo not the same when re-opened")
		} else if _, err := foo.Write([]byte("new foo contents"), 0, fs.WhenceFromStart); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}
		checkClose(t, foo)

		bar = checkOpenFile(t, subdir, "bar", fs.OpenFlagRead)
		readBuf = make([]byte, len(barContents))
		checkRead(t, bar, readBuf, 0, fs.WhenceFromStart)
		if !bytes.Equal(readBuf, []byte(barContents)) {
			t.Fatal("File bar not the same when re-opened")
		} else if _, err := bar.Write([]byte("new bar contents"), 0, fs.WhenceFromStart); err != fs.ErrPermission {
			t.Fatal("Expected read only errors, but saw: ", err)
		}
		checkClose(t, bar)

		// Confirm that "write" operations on root do not work
		openFlags := fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagFile
		if _, _, err := root.Open("newFile", openFlags); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}
		openFlags = fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagDirectory
		if _, _, err := root.Open("newDirectory", openFlags); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}
		if err := root.Rename(root, "foo", "foo2"); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		} else if err := root.Unlink("foo"); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}

		// Confirm that "write" operations on a subdirectory do not work
		subdir = checkOpenDirectory(t, root, "subdir", fs.OpenFlagRead)
		openFlags = fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagFile
		if _, _, err := subdir.Open("newFile", openFlags); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}
		openFlags = fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagDirectory
		if _, _, err := subdir.Open("newDirectory", openFlags); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}
		if err := subdir.Rename(subdir, "bar", "bar2"); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		} else if err := subdir.Unlink("bar"); err != fs.ErrPermission {
			t.Fatal("Expected read only error, but saw: ", err)
		}

		// Close the root, unmount the filesystem
		checkClose(t, subdir)
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	glog.Info("Testing FAT32 Readonly")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16 Readonly")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test creation of files and directories with invalid names
func TestBadNames(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		flags := fs.OpenFlagRead | fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagFile
		if _, _, err := root.Open("this_filename\x00contains_null", flags); err == nil {
			t.Fatal("Expected error")
		} else if _, _, err := root.Open("this_filename\\contains_slash", flags); err == nil {
			t.Fatal("Expected error")
		}

		flags = fs.OpenFlagRead | fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagDirectory
		if _, _, err := root.Open("dirname\x00with_null", flags); err == nil {
			t.Fatal("Expected error")
		} else if _, _, err := root.Open("dirname\\with_slash", flags); err == nil {
			t.Fatal("Expected error")
		}

		// Close the filesystem
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Tests renaming to and from invalid targets
func TestRenameInvalid(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		renameTestsInDirectory := func(d fs.Directory) {
			exclusiveCreateFlags := fs.OpenFlagRead | fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagExclusive
			// Test invalid sources
			if err := d.Rename(d, "source_that_doesn't_exist", "dst"); err != fs.ErrNotFound {
				t.Fatal("Expected error; source doesn't exist")
			} else if err := d.Rename(d, ".", "dst"); err != fs.ErrIsActive {
				t.Fatal("Expected error; . does exist, but it should be busy")
			} else if err := d.Rename(d, "..", "dst"); err != fs.ErrIsActive {
				t.Fatal("Expected error; .. does exist, but it should be busy")
			}

			// Test invalid destinations (with input file)
			filename := "foo.txt"
			foo := checkOpenFile(t, d, filename, exclusiveCreateFlags)
			if err := d.Rename(d, filename, "."); err != fs.ErrIsActive {
				t.Fatal("Expected error: . does exist, but it should be busy")
			} else if err := d.Rename(d, filename, ".."); err != fs.ErrIsActive {
				t.Fatal("Expected error: .. does exist, but it should be busy")
			} else if err := d.Rename(d, filename, filename); err != fs.ErrIsActive {
				t.Fatal("Expected error: file does exist, but it should be busy")
			} else if err := d.Rename(d, filename, "target_parent_dir/does_not_exist"); err != fs.ErrNotFound {
				t.Fatal("Expected error: source exists, but the target's parent directory does not")
			}
			checkClose(t, foo)

			// Test invalid destinations (with input directory)
			dirname := "bar"
			bar := checkOpenDirectory(t, d, dirname, exclusiveCreateFlags)
			if err := d.Rename(d, dirname, "."); err != fs.ErrIsActive {
				t.Fatal("Expected error; . does exist, but it should be busy")
			} else if err := d.Rename(d, dirname, ".."); err != fs.ErrIsActive {
				t.Fatal("Expected error; .. does exist, but it should be busy")
			} else if err := d.Rename(d, dirname, dirname); err != fs.ErrIsActive {
				t.Fatal("Expected error; directory does exist, but it should be busy")
			}

			// Test renaming directory to target directory where target is not closed
			overwriteName := "overwrite_me"
			overwriteDir := checkOpenDirectory(t, d, overwriteName, exclusiveCreateFlags)
			checkRename(t, d, dirname, overwriteName)
			checkRename(t, d, overwriteName, dirname)
			checkClose(t, overwriteDir)

			// Test renaming file to directory and vice-versa
			if err := d.Rename(d, filename, dirname); err != fs.ErrNotADir {
				t.Fatal("Expected error: Should not be able to rename a file to a directory")
			} else if err := d.Rename(d, dirname, filename); err != fs.ErrNotADir {
				t.Fatal("Expected error: Should not be able to rename a directory to a file")
			}

			// Test cases of renaming a directory to a subdirectory of itself
			subdirname := "baz"
			baz := checkOpenDirectory(t, bar, subdirname, exclusiveCreateFlags)
			checkClose(t, baz)
			if err := bar.Rename(bar, subdirname, subdirname+"/blat"); err != fs.ErrInvalidArgs {
				// bar/baz -> bar/baz/blat
				t.Fatal("Expected error: Should not be able to make a directory a subdirectory of itself")
			} else if err := bar.Rename(bar, subdirname, subdirname+"/blat/blah"); err != fs.ErrNotFound {
				// bar/baz -> bar/baz/blat/blah
				t.Fatal("Expected error: Subdirectory does not exist")
			} else if err := bar.Rename(bar, subdirname, subdirname+"/blat"); err != fs.ErrInvalidArgs {
				// bar/baz -> bar/baz/blat
				t.Fatal("Expected error: Should not be able to make a directory a subdirectory of itself")
			} else if err := bar.Rename(bar, subdirname, "./"+subdirname+"/./blat"); err != fs.ErrInvalidArgs {
				// bar/baz -> bar/./baz/./blat
				t.Fatal("Expected error: Should not be able to make a directory a subdirectory of itself")
			}
			checkClose(t, bar)

			// Test case where destination is non-empty directory
			// bat -> bar, but bar contains baz
			bat := checkOpenDirectory(t, d, "bat", exclusiveCreateFlags)
			if err := d.Rename(d, "bat", dirname); err != fs.ErrNotEmpty {
				t.Fatal("Expected error: Should not be able to (via rename) overwrite non-empty directory")
			}
			checkClose(t, bat)
			checkUnlink(t, d, "bat")

			checkUnlink(t, d, filename)
			checkUnlink(t, d, dirname+"/"+subdirname)
			checkUnlink(t, d, dirname)
		}

		// Test the failure cases inside the root directory
		renameTestsInDirectory(root)

		// Test the failure cases when ".." refers to root
		subDir := checkOpenDirectory(t, root, "subdir", fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate)
		renameTestsInDirectory(subDir)

		// Test the failure cases when ".." refers to a non-root directory
		subSubDir := checkOpenDirectory(t, subDir, "subsubdir", fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate)
		renameTestsInDirectory(subSubDir)

		// Close the filesystem
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test simple cases of renaming:
//	 - Rename file to new location (non-overwrite)
//	 - Rename file to new location (overwrite)
//	 - Rename directory to new location (non-overwrite)
//	 - Rename directory to new location (overwrite)
func TestRenameSimple(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		exclusiveCreateFlags := fs.OpenFlagRead | fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagExclusive
		renameTestsInDirectory := func(renameBaseDir fs.Directory, srcPfx, dstPfx string) {
			checkRenameAndBack := func(src, target string) {
				checkRename(t, renameBaseDir, src, target)
				// Verify that this operation removed the original file
				checkExists(t, renameBaseDir, target)
				checkDoesNotExist(t, renameBaseDir, src)
				// Rename back to the original file
				checkRename(t, renameBaseDir, target, src)
				checkExists(t, renameBaseDir, src)
				checkDoesNotExist(t, renameBaseDir, target)
			}

			// Test renaming a single file
			srcName := srcPfx + "foo.txt"
			targetName := dstPfx + "foo_renamed.txt"
			foo := checkOpenFile(t, renameBaseDir, srcName, exclusiveCreateFlags)

			for i := 0; i < 2; i++ {
				// Rename: File --> File that doesn't exist
				checkExists(t, renameBaseDir, srcName)
				checkDoesNotExist(t, renameBaseDir, targetName)
				checkRenameAndBack(srcName, targetName)

				// Rename: File --> Files that DOES exist
				targetName = dstPfx + "overwrite_me.txt"
				overwriteMe := checkOpenFile(t, renameBaseDir, targetName, exclusiveCreateFlags)
				checkExists(t, renameBaseDir, srcName)
				checkExists(t, renameBaseDir, targetName)
				checkRenameAndBack(srcName, targetName)
				// ... Even with all this renaming, the overwritten file should still be closeable
				checkClose(t, overwriteMe)

				if i == 0 {
					// Try these operations, once with the file open, and once with the file closed.
					checkClose(t, foo)
				}
			}
			checkUnlink(t, renameBaseDir, srcName)

			// Test renaming a single directory
			srcName = srcPfx + "bar"
			targetName = dstPfx + "bar_renamed"
			bar := checkOpenDirectory(t, renameBaseDir, srcName, exclusiveCreateFlags)

			for i := 0; i < 2; i++ {
				// Rename: Dir --> Dir that doesn't exist
				checkExists(t, renameBaseDir, srcName)
				checkDoesNotExist(t, renameBaseDir, targetName)
				checkRenameAndBack(srcName, targetName)

				// Rename: Dir --> Dir that DOES exist, is closed, and is empty.
				targetName = dstPfx + "overwrite_me"
				checkClose(t, checkOpenDirectory(t, renameBaseDir, targetName, exclusiveCreateFlags))
				checkExists(t, renameBaseDir, srcName)
				checkExists(t, renameBaseDir, targetName)
				checkRenameAndBack(srcName, targetName)
				if i == 0 {
					// Try these operations, once with the directory open, and once with the
					// directory closed.
					checkClose(t, bar)
				}
			}

			// Rename: Dir --> Dir that DOES exist, is NOT closed, and is empty.
			targetDir := checkOpenDirectory(t, renameBaseDir, targetName, exclusiveCreateFlags)
			checkExists(t, renameBaseDir, srcName)
			checkExists(t, renameBaseDir, targetName)
			checkRenameAndBack(srcName, targetName)
			// Target should exist, but shouldn't be writable (it no longer has a name)
			targetContents := checkReadDir(t, targetDir, 2)
			checkDirent(t, targetContents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, targetContents[1], "..", fs.FileTypeDirectory)
			if _, _, err := targetDir.Open("foo", exclusiveCreateFlags|fs.OpenFlagFile); err != fs.ErrFailedPrecondition {
				t.Fatal("Expected error writing to deleted dir, but saw: ", err)
			}
			checkClose(t, targetDir)

			checkUnlink(t, renameBaseDir, srcName)
		}

		renameTestsInDirectory(root, "", "")

		subDir := checkOpenDirectory(t, root, "subdir", exclusiveCreateFlags)
		renameTestsInDirectory(subDir, "", "")
		renameTestsInDirectory(root, "subdir/", "")
		renameTestsInDirectory(root, "", "subdir/")

		subSubDir := checkOpenDirectory(t, subDir, "subsubdir", exclusiveCreateFlags)
		renameTestsInDirectory(subDir, "", "")
		renameTestsInDirectory(subDir, "subsubdir/", "")
		renameTestsInDirectory(subDir, "", "subsubdir/")
		renameTestsInDirectory(root, "subdir/subsubdir/", "subdir/")
		renameTestsInDirectory(root, "subdir/", "subdir/subsubdir/")

		checkClose(t, subDir)
		checkClose(t, subSubDir)

		// Close the filesystem
		checkClose(t, root)
		checkClose(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test renaming between directories.
// This may seem like a somewhat contrived case (double-open + rename) but
// it is intended to prevent a regression against a real refcounting bug that
// has occurred in the past.
func TestRenameInterDirectory(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		// Verify that moving a "twice-opened-file" into a new directory preserves
		// the refcount upon close.
		exclusiveCreateFlags := fs.OpenFlagRead | fs.OpenFlagWrite | fs.OpenFlagCreate | fs.OpenFlagExclusive
		subDir := checkOpenDirectory(t, root, "subdir", exclusiveCreateFlags)
		dstfile := checkOpenFile(t, subDir, "srcfile", exclusiveCreateFlags)
		checkClose(t, dstfile)
		checkClose(t, subDir)

		srcfile := checkOpenFile(t, root, "srcfile", exclusiveCreateFlags)
		srcfile2 := checkOpenFile(t, root, "srcfile", fs.OpenFlagRead)
		checkRename(t, root, "srcfile", "subdir/srcfile")
		checkClose(t, srcfile)
		checkClose(t, srcfile2)

		checkUnlink(t, root, "subdir/srcfile")
		checkUnlink(t, root, "subdir")

		// Verify the same thing for a "twice-opened-directory".

		subDir = checkOpenDirectory(t, root, "subdir", exclusiveCreateFlags)
		dstDir := checkOpenDirectory(t, subDir, "src", exclusiveCreateFlags)
		checkClose(t, dstDir)
		checkClose(t, subDir)

		srcdir := checkOpenDirectory(t, root, "src", exclusiveCreateFlags)
		srcdir2 := checkOpenDirectory(t, root, "src", fs.OpenFlagRead)
		checkRename(t, root, "src", "subdir/src")
		checkClose(t, srcdir)
		checkClose(t, srcdir2)

		checkUnlink(t, root, "subdir/src")
		checkUnlink(t, root, "subdir")

		// Close the filesystem
		checkClose(t, root)
		checkClose(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test simple cases of unlinking:
//	- Unlinking files and directories which are closed
//	- Unlinking files and directories which are open
//	- Accessing files after they have been unlinked
//	- Failing to unlink non-empty directories
func TestUnlinkSimple(t *testing.T) {
	testFileRemovalSimple := func(d fs.Directory, filename string) {
		// Create a new file
		foo := checkOpenFile(t, d, filename, fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagExclusive)

		// Write to the file
		fooContents := "This is the data inside file foo"
		checkWrite(t, foo, []byte(fooContents), 0, fs.WhenceFromStart)

		// Close the file
		checkClose(t, foo)
		checkDirectoryContains(t, d, filename, fs.FileTypeRegularFile, 3)

		// Unlink the file
		checkUnlink(t, d, filename)
		checkDirectoryEmpty(t, d)
		checkDoesNotExist(t, d, filename)
	}

	testFileUseAfterUnlink := func(d fs.Directory, filename string) {
		// Create a new file
		foo := checkOpenFile(t, d, filename, fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagExclusive)

		// Write to the file
		fooContents := "This is the data inside file foo"
		checkWrite(t, foo, []byte(fooContents), 0, fs.WhenceFromStart)

		// Unlink the file
		checkUnlink(t, d, filename)
		checkDirectoryEmpty(t, d)
		checkDoesNotExist(t, d, filename)

		// Write to the file again, after being unlinked
		fooContents = "Hang on, let me change the contents of file foo"
		checkWrite(t, foo, []byte(fooContents), 0, fs.WhenceFromStart)
		checkDirectoryEmpty(t, d)

		// Close the file
		checkClose(t, foo)
		checkDirectoryEmpty(t, d)
		checkDoesNotExist(t, d, filename)
	}

	testDirectoryRemoval := func(d fs.Directory, subdirname, subfilename string) {
		// Create a new subdirectory, and create a file within that subdirectory
		subdir := checkOpenDirectory(t, d, subdirname, fs.OpenFlagWrite|fs.OpenFlagRead|fs.OpenFlagCreate|fs.OpenFlagExclusive)
		subfile := checkOpenFile(t, subdir, subfilename, fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagExclusive)

		// Verify the parent directory contains the subd, and the subdir contains the subfile
		checkDirectoryContains(t, d, subdirname, fs.FileTypeDirectory, 3)
		checkDirectoryContains(t, subdir, subfilename, fs.FileTypeRegularFile, 3)

		// Try (and fail) to unlink the subdirectory. Verify nothing was removed
		checkClose(t, subdir)
		if err := d.Unlink(subdirname); err != fs.ErrNotEmpty {
			t.Fatal("Expected error ErrNotEmpty, saw: ", err)
		}
		subdir = checkOpenDirectory(t, d, subdirname, fs.OpenFlagWrite|fs.OpenFlagRead)

		checkDirectoryContains(t, d, subdirname, fs.FileTypeDirectory, 3)
		checkDirectoryContains(t, subdir, subfilename, fs.FileTypeRegularFile, 3)

		// Try (and succeed) at removing the subfile
		checkUnlink(t, subdir, subfilename)
		checkDoesNotExist(t, subdir, subfilename)
		checkDirectoryContains(t, d, subdirname, fs.FileTypeDirectory, 3)
		checkDirectoryEmpty(t, subdir)

		// Try (and succeed) to unlink the subdirectory
		checkDirectoryContains(t, d, subdirname, fs.FileTypeDirectory, 3)
		checkUnlink(t, d, subdirname)
		checkDirectoryEmpty(t, d)
		checkDirectoryEmpty(t, subdir)
		checkClose(t, subdir)

		// Clean up the subfile, which was unlinked a while ago
		checkClose(t, subfile)
	}

	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		// Try running tests with different file/directory names, so we can test a variable number
		// of direntries to be deleted.
		testFileRemovalSimple(root, "foo.txt")
		testFileRemovalSimple(root, "foooooooobar is a long name.txt")
		testFileRemovalSimple(root, "FOO.TXT")
		testFileUseAfterUnlink(root, "foo.txt")
		testFileUseAfterUnlink(root, "foooooooobar is a long name.txt")
		testFileUseAfterUnlink(root, "FOO.TXT")
		testDirectoryRemoval(root, "subdir", "foo.txt")
		testDirectoryRemoval(root, "long subdirectory name", "foooooooobar is a long name.txt")
		testDirectoryRemoval(root, "SUBDIR", "FOO.TXT")

		subdir := checkOpenDirectory(t, root, "test_subdirectory", fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate)

		testFileRemovalSimple(subdir, "foo.txt")
		testFileRemovalSimple(subdir, "foooooooobar is a long name.txt")
		testFileRemovalSimple(subdir, "FOO.TXT")
		testFileUseAfterUnlink(subdir, "foo.txt")
		testFileUseAfterUnlink(subdir, "foooooooobar is a long name.txt")
		testFileUseAfterUnlink(subdir, "FOO.TXT")
		testDirectoryRemoval(subdir, "subdir", "foo.txt")
		testDirectoryRemoval(subdir, "long subdirectory name", "foooooooobar is a long name.txt")
		testDirectoryRemoval(subdir, "SUBDIR", "FOO.TXT")

		// Close the filesystem
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test cases of unlink where an error is expected
func TestUnlinkInvalid(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		if err := root.Unlink("/"); err != fs.ErrInvalidArgs {
			t.Fatal(err)
			t.Fatal("Expected error unlinking root")
		} else if err := root.Unlink("."); err != fs.ErrIsActive {
			t.Fatal("Expected error unlinking root")
		} else if err := root.Unlink(".."); err != fs.ErrIsActive {
			t.Fatal("Expected error unlinking root")
		}

		subdir := checkOpenDirectory(t, root, "test_subdirectory", fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate)

		if err := subdir.Unlink("."); err != fs.ErrIsActive {
			t.Fatal("Expected error unlinking subdirectory")
		} else if err := subdir.Unlink(".."); err != fs.ErrIsActive {
			t.Fatal("Expected error unlinking subdirectory")
		} else if err := subdir.Unlink("foo"); err != fs.ErrNotFound {
			t.Fatal("Expected error unlinking missing file")
		}

		// Close the filesystem
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test observing and modifying the "last modified time" of files and directories
func TestTime(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		// Test creating / stat-ing / touching a node (file or directory)
		testNode := func(flags fs.OpenFlags) {
			name := "test_node"
			n := checkOpen(t, root, name, flags|fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate)
			_, _, timeCreate := checkStat(t, n)
			if timeCreate.Unix() < time.Now().Unix()-2 || timeCreate.Unix() > time.Now().Unix() {
				t.Fatal("File should have been made in the last couple seconds")
			}

			// FAT timestamps have a granularity of 2 seconds. Wait for 3 second to make it likely that
			// the timestamp will be updated.
			time.Sleep(3 * time.Second)
			touchTime := time.Now()
			checkTouch(t, n, touchTime, touchTime)
			_, _, timeUpdate := checkStat(t, n)
			if timeUpdate.Unix() <= timeCreate.Unix() {
				t.Fatal("Expected time to be updated after touch")
			}

			// Try closing and re-opening the directory to verify that timestamp was saved to disk.
			checkClose(t, n)
			n = checkOpen(t, root, name, flags|fs.OpenFlagRead)
			_, _, timeReopen := checkStat(t, n)
			// When the "timeUpdate" gets written to disk, it only has two-second granularity.
			timeUpdateDisk := timeUpdate.Unix()
			if timeUpdateDisk%2 != 0 {
				timeUpdateDisk--
			}
			if timeUpdateDisk != timeReopen.Unix() {
				t.Fatal("Expected time to be updated after re-opening file")
			}

			checkClose(t, n)
			checkUnlink(t, root, name)
		}

		testNode(fs.OpenFlagFile)
		// This test would fail for directories, but that is the expected behavior of common FAT
		// filesystems.
		// To quote "File System Forensic Analysis", Chapter 9: FAT Concepts and Analysis, page 235:
		//		"For directories... the dates were set when the directory was created and were not
		//		updated much after that. Even when new clusters were allocated for the directory or
		//		new files were created in the directory, the written times were not updated"

		// Close the filesystem
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test using files and directories after they are closed.
func TestUseAfterClose(t *testing.T) {
	checkClosedFileOps := func(f fs.File, goldErr error) {
		buf := []byte{'a'}
		if err := f.Close(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, _, _, err := f.Stat(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := f.Dup(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := f.Reopen(fs.OpenFlagRead); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := f.Read(buf, 0, fs.WhenceFromStart); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := f.Write(buf, 0, fs.WhenceFromStart); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if err := f.Truncate(0); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := f.Tell(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := f.Seek(0, fs.WhenceFromStart); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		}
	}
	checkClosedDirOps := func(d fs.Directory, goldErr error) {
		if err := d.Close(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, _, _, err := d.Stat(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if err := d.Touch(time.Now(), time.Now()); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := d.Dup(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := d.Reopen(fs.OpenFlagRead); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, err := d.Read(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if _, _, err := d.Open("foo", fs.OpenFlagRead); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if err := d.Rename(d, "foo", "bar"); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if err := d.Sync(); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		} else if err := d.Unlink("foo"); err != goldErr {
			t.Fatalf("Expected %s error, saw: %s", goldErr, err)
		}
	}

	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		foo := checkOpenFile(t, root, "foo", fs.OpenFlagWrite|fs.OpenFlagCreate)
		fooContents := "This is the data inside file foo"
		checkWrite(t, foo, []byte(fooContents), 0, fs.WhenceFromStart)
		// Close / Verify foo's closed state
		checkClose(t, foo)
		checkClosedFileOps(foo, fs.ErrNotOpen)

		subdir := checkOpenDirectory(t, root, "subdir", fs.OpenFlagCreate|fs.OpenFlagRead|fs.OpenFlagWrite)
		bar := checkOpenFile(t, subdir, "bar", fs.OpenFlagWrite|fs.OpenFlagCreate)
		barContents := "This is different data inside file bar"
		checkWrite(t, bar, []byte(barContents), 0, fs.WhenceFromStart)

		// Close / Verify subdir and bar's closed state
		checkClose(t, subdir)
		checkClosedDirOps(subdir, fs.ErrNotOpen)
		checkClose(t, bar)
		checkClosedFileOps(bar, fs.ErrNotOpen)

		// Re-open all files / directories
		foo = checkOpenFile(t, root, "foo", fs.OpenFlagRead)
		subdir = checkOpenDirectory(t, root, "subdir", fs.OpenFlagRead)
		bar = checkOpenFile(t, subdir, "bar", fs.OpenFlagRead)

		// Close the filesystem, verify the root is closed
		checkClose(t, root)
		checkClosedDirOps(root, fs.ErrNotOpen)
		checkCloseFS(t, fatFS)

		// Verify that all files / directories are unmounted and inaccessible
		checkClosedFileOps(foo, fs.ErrUnmounted)
		checkClosedFileOps(bar, fs.ErrUnmounted)
		checkClosedDirOps(subdir, fs.ErrUnmounted)
		checkClosedDirOps(root, fs.ErrUnmounted)
	}

	glog.Info("Testing FAT32")
	fileBackedFAT, dev := setupFAT32(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	glog.Info("Testing FAT16")
	fileBackedFAT, dev = setupFAT16(t)
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

// Test simple path traversal in a non-changing directory structure.
func TestPathTraversalStatic(t *testing.T) {
	doTest := func(dev block.Device) {
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		rootContents := checkReadDir(t, root, 2)
		checkDirent(t, rootContents[0], ".", fs.FileTypeDirectory)
		checkDirent(t, rootContents[1], "..", fs.FileTypeDirectory)

		// Make a subdirectory, Verify that the new directory is empty.
		flags := fs.OpenFlagCreate | fs.OpenFlagWrite | fs.OpenFlagRead | fs.OpenFlagExclusive
		foo := checkOpenDirectory(t, root, "foo", flags)
		fiz := checkOpenDirectory(t, root, "foo/fiz", flags)
		fizFile := checkOpenFile(t, root, "foo/fiz/file.txt", flags)
		checkClose(t, fizFile)
		bar := checkOpenDirectory(t, root, "foo/bar", flags)
		baz := checkOpenDirectory(t, root, "foo/bar/baz", flags)

		// Verify the directory structure has been created
		checkValidRoot := func(d fs.Directory) {
			contents := checkReadDir(t, d, 3)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
			checkDirent(t, contents[2], "foo", fs.FileTypeDirectory)
		}
		checkValidRoot(root)

		checkValidFoo := func(d fs.Directory) {
			contents := checkReadDir(t, d, 4)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
			checkDirent(t, contents[2], "fiz", fs.FileTypeDirectory)
			checkDirent(t, contents[3], "bar", fs.FileTypeDirectory)
		}
		checkValidFoo(foo)

		checkValidFiz := func(d fs.Directory) {
			contents := checkReadDir(t, d, 3)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
			checkDirent(t, contents[2], "file.txt", fs.FileTypeRegularFile)
		}
		checkValidFiz(fiz)

		checkValidBar := func(d fs.Directory) {
			contents := checkReadDir(t, d, 3)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
			checkDirent(t, contents[2], "baz", fs.FileTypeDirectory)
		}
		checkValidBar(bar)

		checkValidBaz := func(d fs.Directory) {
			contents := checkReadDir(t, d, 2)
			checkDirent(t, contents[0], ".", fs.FileTypeDirectory)
			checkDirent(t, contents[1], "..", fs.FileTypeDirectory)
		}
		checkValidBaz(baz)

		dir := checkOpenDirectory(t, root, ".", fs.OpenFlagRead) // In root, open self
		checkValidRoot(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, root, "..", fs.OpenFlagRead) // In root, open self via ".."
		checkValidRoot(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, root, "../..", fs.OpenFlagRead) // In root, open self via "../..
		checkValidRoot(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, foo, ".", fs.OpenFlagRead) // In "/foo", open foo
		checkValidFoo(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, foo, "..", fs.OpenFlagRead) // In "/foo", open self via ".."
		checkValidFoo(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, foo, "bar/baz", fs.OpenFlagRead) // In "/foo", open baz
		checkValidBaz(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, foo, "fiz", fs.OpenFlagRead) // In "/foo", open fiz
		checkValidFiz(dir)
		checkClose(t, dir)

		dir = checkOpenDirectory(t, foo, "..///./../", fs.OpenFlagRead) // In "/foo", open "foo"
		checkValidFoo(dir)
		checkClose(t, dir)

		// Close all the original copies of the directories we had open
		checkClose(t, foo)
		checkClose(t, fiz)
		checkClose(t, bar)
		checkClose(t, baz)
		checkClose(t, root)
		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	glog.Info("FAT32")
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	glog.Info("FAT16")
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}

func deleteAllInDirectory(d fs.Directory, path string, maxNumToDelete int) (numDeleted int) {
	// Read the contents of a directory
	entries, err := d.Read()
	if err != nil {
		panic(err)
	}
	// Try to unlink any non "." and non ".." entries
	for i := range entries {
		if maxNumToDelete == 0 {
			return
		} else if entries[i].GetName() != "." && entries[i].GetName() != ".." {
			// Do NOT make an assumption about the type of the entry. It may be deleted / recreated
			// by another thread.
			err := d.Unlink(entries[i].GetName())
			if err == nil {
				// We successfully deleted a file / directory
				numDeleted++
				maxNumToDelete--
				continue
			} else if err == fs.ErrNotFound {
				// Someone else deleted the file / directory
				continue
			} else if err == fs.ErrNotEmpty {
				// If the directory is not empty, try to delete its contents
				if _, subdir, _ := d.Open(entries[i].GetName(), fs.OpenFlagRead|fs.OpenFlagDirectory); subdir != nil {
					subDeleted := deleteAllInDirectory(subdir, path+entries[i].GetName()+"/", maxNumToDelete)
					numDeleted += subDeleted
					maxNumToDelete -= subDeleted
					if err := subdir.Close(); err != nil {
						panic(err)
					}
				}
			} else if err != fs.ErrIsActive {
				panic(err)
			}
		}
	}
	return
}

// Used to generate random file / directory names
func randomName() string {
	letters := []rune("abcdefABCDEF01234")
	b := make([]rune, 1+rand.Intn(10))
	for i := range b {
		b[i] = letters[rand.Intn(len(letters))]
	}
	return string(b)
}

const (
	choiceCreateFile     = iota // Create a file and immediately close it
	choiceCreateDir             // Create a directory and immediately close it
	choiceCreateDirEnter        // Create a directory and enter it
	numChoices
)

func createInDirectory(d fs.Directory, path string, maxNumToCreate int) (numCreated int) {
	if maxNumToCreate == 0 {
		return
	}
	choice := rand.Intn(numChoices)
	name := randomName()
	switch choice {
	case choiceCreateFile:
		n, _, err := d.Open(name, fs.OpenFlagWrite|fs.OpenFlagFile|fs.OpenFlagCreate|fs.OpenFlagExclusive)
		if err == fs.ErrNotAFile || err == fs.ErrAlreadyExists {
			// 'name' already exists
			return
		} else if err == fs.ErrNoSpace {
			// Disk (or directory) space is full
			return
		} else if err != nil {
			panic(err)
		} else if err := n.Close(); err != nil {
			panic(err)
		}
		numCreated++
	case choiceCreateDir, choiceCreateDirEnter:
		_, n, err := d.Open(name, fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagDirectory|fs.OpenFlagCreate|fs.OpenFlagExclusive)
		if err == fs.ErrNotADir || err == fs.ErrAlreadyExists {
			// 'name' already exists
			return
		} else if err == fs.ErrNoSpace {
			// Disk (or directory) space is full
			return
		} else if err != nil {
			panic(err)
		}

		if choice == choiceCreateDirEnter {
			numCreated += createInDirectory(n, path+name+"/", maxNumToCreate-1)
		}

		if err := n.Close(); err != nil {
			panic(err)
		}
		numCreated++
	}
	return
}

func TestConcurrentOpenDelete(t *testing.T) {
	doTest := func(dev block.Device) {
		rand.Seed(time.Now().Unix())
		fatFS := checkNewFS(t, dev, fs.ReadWrite)
		root := fatFS.RootDirectory()

		done := make(chan bool)
		// A deleter thread reads a directory and attempts to unlink files
		deleter := func(numToDelete int) {
			for numToDelete > 0 {
				numToDelete -= deleteAllInDirectory(root, "/", numToDelete)
			}
			done <- true
		}

		// A creator thread randomly makes files and directories
		creator := func(numToCreate int) {
			for numToCreate > 0 {
				delta := createInDirectory(root, "/", numToCreate)
				numToCreate -= delta
				if delta == 0 {
					// Purely for performance: avoid churning when root / filesystem is full
					time.Sleep(10 * time.Microsecond)
				}
			}
			done <- true
		}

		n := 1000
		numCreators := 5
		numDeleters := 5
		for i := 0; i < numCreators; i++ {
			go creator(n)
		}
		for i := 0; i < numDeleters; i++ {
			go deleter(n)
		}
		for i := 0; i < numCreators+numDeleters; i++ {
			<-done
		}

		checkCloseFS(t, fatFS)
	}

	fileBackedFAT, dev := setupFAT32(t)
	glog.Info("FAT32")
	doTest(dev)
	cleanup(fileBackedFAT, dev)

	fileBackedFAT, dev = setupFAT16(t)
	glog.Info("FAT16")
	doTest(dev)
	cleanup(fileBackedFAT, dev)
}
