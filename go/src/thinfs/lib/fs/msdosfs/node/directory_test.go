// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/direntry"
)

func checkRead(t *testing.T, d DirectoryNode, index int, goldName string, goldCluster uint32, goldNumSlots int) {
	entry, numSlots, err := Read(d, index)
	if err != nil {
		t.Fatal(err)
	} else if numSlots != goldNumSlots {
		t.Fatalf("Unexpected number of slots: %d (expected %d)", numSlots, goldNumSlots)
	} else if entry.GetName() != goldName {
		t.Fatalf("Unexpected name: %s (expected %s)", entry.GetName(), goldName)
	} else if entry.Cluster != goldCluster {
		t.Fatalf("Unexpected cluster %d (expected %d)", entry.Cluster, goldCluster)
	}

	entry, observedIndex, err := Lookup(d, entry.GetName())
	if err != nil {
		t.Fatal(err)
	} else if observedIndex != index {
		t.Fatalf("Found %s at index %d (expected %d)", goldName, observedIndex, index)
	} else if entry.GetName() != goldName {
		t.Fatalf("Unexpected name: %s (expected %s)", entry.GetName(), goldName)
	} else if entry.Cluster != goldCluster {
		t.Fatalf("Unexpected cluster %d (expected %d)", entry.Cluster, goldCluster)
	}
}

func checkIsEmpty(t *testing.T, d DirectoryNode, goldEmpty bool) {
	if empty, err := IsEmpty(d); err != nil {
		t.Fatal(err)
	} else if empty != goldEmpty {
		t.Fatal("Unexpected empty status")
	}
}

func checkAllocate(t *testing.T, d DirectoryNode, name string, cluster uint32, attr fs.FileType, goldIndex int) {
	entry := direntry.New(name, cluster, attr)
	if index, err := Allocate(d, entry); err != nil {
		t.Fatal(err)
	} else if index != goldIndex {
		t.Fatalf("Allocated direntry to unexpected index %d (expected %d)", index, goldIndex)
	}
}

func checkWriteDotAndDotDot(t *testing.T, d DirectoryNode, cluster, parentCluster uint32) {
	if err := WriteDotAndDotDot(d, cluster, parentCluster); err != nil {
		t.Fatal(err)
	}
}

func checkMakeEmpty(t *testing.T, d DirectoryNode) {
	if err := MakeEmpty(d); err != nil {
		t.Fatal(err)
	}
}

func checkUpdate(t *testing.T, d DirectoryNode, child Node, index int) {
	if _, err := Update(d, child.StartCluster(), child.MTime(), uint32(child.Size()), index); err != nil {
		t.Fatal(err)
	}
}

func checkFree(t *testing.T, d DirectoryNode, index int) {
	if _, err := Free(d, index); err != nil {
		t.Fatal(err)
	}
}

func TestDirentEmptyDirectory(t *testing.T) {
	testDirectory := func(d DirectoryNode) {
		if d.IsRoot() {
			checkIsEmpty(t, d, true)
		}

		// Create the "typical" contents for a directory: ".", "..", and the last free directory.
		dotCluster := uint32(12)
		dotdotCluster := uint32(34)
		if !d.IsRoot() {
			checkWriteDotAndDotDot(t, d, dotCluster, dotdotCluster)
		}
		checkMakeEmpty(t, d)
		if !d.IsRoot() {
			checkRead(t, d, 0, ".", dotCluster, 1)
			checkRead(t, d, 1, "..", dotdotCluster, 1)
		}
		checkIsEmpty(t, d, true)

		// Try adding a directory
		fooCluster := uint32(56)
		goldIndex := int(0)
		if !d.IsRoot() {
			goldIndex = int(2)
		}
		checkAllocate(t, d, "FOO", fooCluster, fs.FileTypeRegularFile, goldIndex)
		if !d.IsRoot() {
			checkRead(t, d, 0, ".", dotCluster, 1)
			checkRead(t, d, 1, "..", dotdotCluster, 1)
		}
		checkRead(t, d, goldIndex, "FOO", fooCluster, 1)
		checkIsEmpty(t, d, false)

		// Try removing that directory
		checkFree(t, d, goldIndex)
		if !d.IsRoot() {
			checkRead(t, d, 0, ".", dotCluster, 1)
			checkRead(t, d, 1, "..", dotdotCluster, 1)
		}
		checkIsEmpty(t, d, true)
	}

	doTest := func(metadata *Metadata, fat32 bool) {
		root := checkedMakeRoot(t, metadata, fat32)
		testDirectory(root)
		foo := checkedMakeDirectoryNode(t, metadata, root, 0)
		testDirectory(foo)
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	doTest(metadata /* fat32= */, true)
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "10M", false)
	doTest(metadata /* fat32= */, false)
	cleanup(fileBackedFAT, metadata)
}

func TestDirentAllocateFree(t *testing.T) {
	doTest := func(metadata *Metadata, fat32 bool) {
		root := checkedMakeRoot(t, metadata, fat32)

		d := checkedMakeDirectoryNode(t, metadata, root, 0)
		checkIsEmpty(t, d, true)

		// Create the "typical" contents for a directory: ".", "..", and the last free directory.
		dotCluster := uint32(12)
		dotdotCluster := uint32(34)
		checkWriteDotAndDotDot(t, d, dotCluster, dotdotCluster)
		checkMakeEmpty(t, d)

		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkIsEmpty(t, d, true)

		// Try adding some direntries
		fooCluster := uint32(56)
		barCluster := uint32(78)
		bazCluster := uint32(90)
		checkAllocate(t, d, "FOO", fooCluster, fs.FileTypeRegularFile, 2)
		checkAllocate(t, d, "bar has a long name", barCluster, fs.FileTypeRegularFile, 3)
		checkAllocate(t, d, "BAZ", bazCluster, fs.FileTypeRegularFile, 6)
		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkRead(t, d, 2, "FOO", fooCluster, 1)
		checkRead(t, d, 3, "bar has a long name", barCluster, 3)
		checkRead(t, d, 6, "BAZ", bazCluster, 1)

		// Try removing some direntries
		checkFree(t, d, 2) // This deletes "FOO"
		checkFree(t, d, 6) // This deletes "BAZ"
		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkRead(t, d, 3, "bar has a long name", barCluster, 3)

		// Try allocating a direntry which won't fill the middle -- it'll require allocation at the
		// end.
		anotherCluster := uint32(1234)
		checkAllocate(t, d, "Another long name", anotherCluster, fs.FileTypeDirectory, 6)
		checkAllocate(t, d, "FOO", fooCluster, fs.FileTypeRegularFile, 2)
		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkRead(t, d, 2, "FOO", fooCluster, 1)
		checkRead(t, d, 3, "bar has a long name", barCluster, 3)
		checkRead(t, d, 6, "Another long name", anotherCluster, 3)
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	doTest(metadata /* fat32= */, true)
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "10M", false)
	doTest(metadata /* fat32= */, false)
	cleanup(fileBackedFAT, metadata)
}

func TestDirentUpdate(t *testing.T) {
	doTest := func(metadata *Metadata, fat32 bool) {
		root := checkedMakeRoot(t, metadata, fat32)

		d := checkedMakeDirectoryNode(t, metadata, root, 0)
		checkIsEmpty(t, d, true)

		// Create the "typical" contents for a directory: ".", "..", and the last free directory.
		dotCluster := uint32(12)
		dotdotCluster := uint32(34)
		checkWriteDotAndDotDot(t, d, dotCluster, dotdotCluster)
		checkMakeEmpty(t, d)
		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkIsEmpty(t, d, true)

		// Try adding a file and a directory
		fileCluster, err := root.Metadata().ClusterMgr.ClusterExtend(0)
		if err != nil {
			t.Fatal(err)
		}
		dirCluster, err := root.Metadata().ClusterMgr.ClusterExtend(0)
		if err != nil {
			t.Fatal(err)
		}
		fileIndex := int(2)
		dirIndex := int(3)
		checkAllocate(t, d, "FILE", fileCluster, fs.FileTypeRegularFile, fileIndex)
		checkAllocate(t, d, "DIR", dirCluster, fs.FileTypeDirectory, dirIndex)
		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkRead(t, d, fileIndex, "FILE", fileCluster, 1)
		checkRead(t, d, dirIndex, "DIR", dirCluster, 1)
		checkIsEmpty(t, d, false)

		// File Update
		fileContents := []byte("This file contains nothing interesting")
		file, err := NewFile(root.Metadata(), root, fileIndex, fileCluster, time.Now())
		if err != nil {
			t.Fatal(err)
		} else if _, err := file.WriteAt(fileContents, 0); err != nil {
			t.Fatal(err)
		} else if entry, _, err := Read(d, fileIndex); err != nil {
			t.Fatal(err)
		} else if entry.Size != 0 { // We updated the node -- the direntry shouldn't be updated yet
			t.Fatal("Old direntry size changed before Update called")
		}

		// Update the direntry
		checkUpdate(t, d, file, fileIndex)

		if entry, _, err := Read(d, fileIndex); err != nil {
			t.Fatal(err)
		} else if entry.Size != uint32(len(fileContents)) { // Observe that 'size' has actually been changed
			t.Fatal("Direntry size unchanged after Update called")
		}

		// Directory update
		dir, err := NewDirectory(root.Metadata(), dirCluster, time.Now())
		if err != nil {
			t.Fatal(err)
		} else if err := WriteDotAndDotDot(dir, dirCluster, dotCluster); err != nil {
			t.Fatal(err)
		}
		checkMakeEmpty(t, dir)
		checkAllocate(t, dir, "SUBFILE", 1234, fs.FileTypeRegularFile, 2)
		if entry, _, err := Read(d, dirIndex); err != nil {
			t.Fatal(err)
		} else if entry.Size != 0 { // We updated the node -- the direntry shouldn't be updated yet
			t.Fatal("Old direntry size changed before Update called")
		}

		// Update the direntry
		checkUpdate(t, d, dir, dirIndex)

		if entry, _, err := Read(d, dirIndex); err != nil {
			t.Fatal(err)
		} else if entry.Size != 0 { // Observe that 'size' has NOT BEEN CHANGED (directory-specific)
			t.Fatal("Direntry size unchanged after Update called")
		}

		// Try removing all directories
		checkFree(t, d, 2) // This deletes "FILE"
		checkFree(t, d, 3) // This deletes "DIR"
		checkRead(t, d, 0, ".", dotCluster, 1)
		checkRead(t, d, 1, "..", dotdotCluster, 1)
		checkIsEmpty(t, d, true)
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	doTest(metadata /* fat32= */, true)
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "10M", false)
	doTest(metadata /* fat32= */, false)
	cleanup(fileBackedFAT, metadata)
}
