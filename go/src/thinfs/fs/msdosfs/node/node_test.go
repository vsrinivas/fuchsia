// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"bytes"
	"testing"
	"time"

	"github.com/golang/glog"

	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/cluster"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/direntry"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/testutil"
	"fuchsia.googlesource.com/thinfs/thinio"
)

func setupFAT32(t *testing.T, size string, readonly bool) (*testutil.FileFAT, *Metadata) {
	fileBackedFAT := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
	dev := fileBackedFAT.GetRawDevice()
	metadata := &Metadata{
		Dev:      thinio.NewConductor(dev, 8*1024),
		Readonly: readonly,
	}
	var err error
	metadata.Br, err = bootrecord.New(metadata.Dev)
	if err != nil {
		t.Fatal(err)
	} else if metadata.Br.Type() != bootrecord.FAT32 {
		t.Fatal("FAT created, but it was not FAT32")
	}

	metadata.ClusterMgr, err = cluster.Mount(metadata.Dev, metadata.Br, metadata.Readonly)
	if err != nil {
		t.Fatal(err)
	}

	return fileBackedFAT, metadata
}

func setupFAT16(t *testing.T, size string, readonly bool) (*testutil.FileFAT, *Metadata) {
	fileBackedFAT := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
	dev := fileBackedFAT.GetRawDevice()
	metadata := &Metadata{
		Dev:      thinio.NewConductor(dev, 8*1024),
		Readonly: readonly,
	}
	var err error
	metadata.Br, err = bootrecord.New(metadata.Dev)
	if err != nil {
		t.Fatal(err)
	} else if metadata.Br.Type() != bootrecord.FAT16 {
		t.Fatal("FAT created, but it was not FAT16")
	}

	metadata.ClusterMgr, err = cluster.Mount(metadata.Dev, metadata.Br, metadata.Readonly)
	if err != nil {
		t.Fatal(err)
	}

	return fileBackedFAT, metadata
}

func cleanup(fileBackedFAT *testutil.FileFAT, metadata *Metadata) {
	fileBackedFAT.RmfsFAT()
	metadata.Dev.Close()
}

func checkedMakeRoot(t *testing.T, metadata *Metadata, fat32 bool) DirectoryNode {
	if fat32 {
		startCluster := metadata.Br.RootCluster()
		root, err := NewDirectory(metadata, startCluster, time.Time{})
		if err != nil {
			t.Fatal(err)
		} else if root.Metadata() != metadata {
			t.Fatal("Invalid metadata")
		} else if !root.IsDirectory() {
			t.Fatal("Node incorrectly thinks it is not a directory")
		} else if !root.IsRoot() {
			t.Fatal("Node incorrectly thinks it is not root")
		}
		return root
	}

	// FAT 12 / 16 case:
	offsetStart, numRootEntriesMax := metadata.Br.RootReservedInfo()
	direntrySize := int64(direntry.DirentrySize)
	root := NewRoot(metadata, offsetStart, numRootEntriesMax*direntrySize)
	if root.Metadata() != metadata {
		t.Fatal("Invalid metadata")
	} else if !root.IsDirectory() {
		t.Fatal("Node incorrectly thinks it is not a directory")
	} else if !root.IsRoot() {
		t.Fatal("Node incorrectly thinks it is not root")
	}

	return root
}

func checkedMakeFileNode(t *testing.T, metadata *Metadata, parent DirectoryNode, direntIndex int) FileNode {
	node, err := NewFile(metadata, parent, direntIndex, 0, time.Time{})
	if err != nil {
		t.Fatal(err)
	} else if node.IsDirectory() {
		t.Fatal("Expected file, not directory")
	} else if node.Metadata() != metadata {
		t.Fatal("Invalid metadata")
	} else if node.Size() != 0 {
		t.Fatal("node.Size() should be zero")
	} else if node.StartCluster() != 0 {
		t.Fatal("Node should be initialized with a zero cluster")
	} else if node.NumClusters() != 0 {
		t.Fatal("Node should be initialized with no clusters")
	} else if c, exists := parent.ChildFile(direntIndex); c != node || !exists {
		t.Fatal("Child should exist in the parent, but it does not")
	}

	if p, i := node.LockParent(); p != parent || i != direntIndex {
		t.Fatal("node.LockParent() returned the wrong node / index combo")
	} else {
		p.Unlock()
	}

	return node
}

func checkedMakeDirectoryNode(t *testing.T, metadata *Metadata, parent DirectoryNode, direntIndex int) DirectoryNode {
	newCluster, err := metadata.ClusterMgr.ClusterExtend(0)
	if err != nil {
		t.Fatal(err)
	}

	node, err := NewDirectory(metadata, newCluster, time.Time{})
	if err != nil {
		t.Fatal(err)
	} else if !node.IsDirectory() {
		t.Fatal("Expected directory, not file")
	} else if node.Metadata() != metadata {
		t.Fatal("Invalid metadata")
	} else if node.IsRoot() {
		t.Fatal("Node incorrectly thinks it is root")
	} else if node.Size() != int64(metadata.Br.ClusterSize()) {
		t.Fatal("node.Size() should be a single cluster")
	} else if node.StartCluster() != newCluster {
		t.Fatal("Node should be initialized with a known cluster")
	} else if node.NumClusters() != 1 {
		t.Fatal("Node should be initialized with one cluster")
	}
	return node
}

func TestSingleNodeReadWrite(t *testing.T) {
	doTest := func(n Node) {
		// TEST: Normal reads / writes to the file
		buf1 := testutil.MakeRandomBuffer(1234)
		buf2 := testutil.MakeRandomBuffer(5678)
		bufCombined := append(buf1, buf2...)

		verifyBuffer := func(buf []byte) {
			// An 'exact' read should work
			readbuf := make([]byte, len(buf))
			if l, err := n.readAt(readbuf, 0); err != nil {
				t.Fatal(err)
			} else if l != len(readbuf) {
				t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf))
			} else if !bytes.Equal(readbuf, buf) {
				t.Fatal("Bytes read not equal to input bytes (buf exact)")
			}

			// A read of 'one less byte' should work
			readbuf = make([]byte, len(buf)-1)
			if l, err := n.readAt(readbuf, 0); err != nil {
				t.Fatal(err)
			} else if l != len(readbuf) {
				t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf))
			} else if !bytes.Equal(readbuf, buf[:len(buf)-1]) {
				t.Fatal("Bytes read not equal to input bytes (buf minus one byte)")
			}

			// A read of 'one more byte' should work, but also return an EOF error.
			readbuf = make([]byte, len(buf)+1)
			if l, err := n.readAt(readbuf, 0); err != fs.ErrEOF {
				t.Fatal("Expected an EOF error")
			} else if l != len(readbuf)-1 {
				t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf)-1)
			} else if !bytes.Equal(readbuf[:len(readbuf)-1], buf) {
				t.Fatal("Bytes read not equal to input bytes (buf plus one byte)")
			}
		}

		if l, err := n.writeAt(buf1, 0); err != nil {
			t.Fatal(err)
		} else if l != len(buf1) {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, len(buf1))
		} else if n.Size() != int64(len(buf1)) {
			t.Fatalf("Unexpected node size: %d (expected %d)", n.Size(), len(buf1))
		}
		verifyBuffer(buf1)

		if l, err := n.writeAt(buf2, int64(len(buf1))); err != nil {
			t.Fatal(err)
		} else if l != len(buf2) {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, len(buf2))
		} else if n.Size() != int64(len(bufCombined)) {
			t.Fatalf("Unexpected node size: %d (expected %d)", n.Size(), len(bufCombined))
		}
		verifyBuffer(bufCombined)

		// This is somewhat cheating, but force the filesystem to become readonly
		n.Metadata().Readonly = true
		// We can still read
		verifyBuffer(bufCombined)
		// We cannot write
		if _, err := n.writeAt(buf1, 0); err != fs.ErrPermission {
			t.Fatal("Expected ReadOnly error")
		}
		n.Metadata().Readonly = false

		// TEST: Edge cases of reading / writing

		// A large read should return zero bytes; it's out of bounds
		readbuf := make([]byte, 10)
		largeOffset := int64(len(bufCombined) + 10)
		if l, err := n.readAt(readbuf, largeOffset); err != fs.ErrEOF {
			t.Fatal("Expected an EOF error")
		} else if l != 0 {
			t.Fatalf("Unexpected read length: %d (expected %d)", l, 0)
		}

		// A large read should fail with fs.ErrEOF; it's only partially out of bounds
		largeOffset = int64(len(bufCombined) - len(readbuf)/2)
		if l, err := n.readAt(readbuf, largeOffset); err != fs.ErrEOF {
			t.Fatal("Expected an EOF error")
		} else if l != len(readbuf)/2 {
			t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf)/2)
		}

		// A large write can succeed; it will just force the file to allocate clusters
		buf := []byte{'a'}
		readbuf = make([]byte, 1)
		if l, err := n.writeAt(buf, largeOffset); err != nil {
			t.Fatal(err)
		} else if l != 1 {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, 1)
		} else if _, err := n.readAt(readbuf, largeOffset); err != nil {
			t.Fatal(err)
		} else if !bytes.Equal(buf, readbuf) {
			t.Fatal("Read buffer did not equal write buffer")
		}

		// Test negative writes / reads
		if l, err := n.writeAt(buf, -1); err != ErrBadArgument {
			t.Fatal("Expected ErrBadArgument")
		} else if l != 0 {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, 0)
		}
		if l, err := n.readAt(buf, -1); err != ErrBadArgument {
			t.Fatal("Expected ErrBadArgument")
		} else if l != 0 {
			t.Fatalf("Unexpected read length: %d (expected %d)", l, 0)
		}

		// Test empty writes / reads
		var emptybuf []byte
		if n, err := n.writeAt(emptybuf, 0); err != nil {
			t.Fatal(err)
		} else if n != 0 {
			t.Fatalf("Empty write actually wrote %d bytes", n)
		}
		if n, err := n.readAt(emptybuf, 0); err != nil {
			t.Fatal(err)
		} else if n != 0 {
			t.Fatalf("Empty read actually read %d bytes", n)
		}
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)

	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	file := checkedMakeFileNode(t, metadata, root, 1)
	glog.Info("Testing FAT32 File")
	doTest(file)

	cleanup(fileBackedFAT, metadata)
	fileBackedFAT, metadata = setupFAT16(t, "10M", false)

	root = checkedMakeRoot(t, metadata /* fat32= */, false)
	file = checkedMakeFileNode(t, metadata, root, 1)
	glog.Info("Testing FAT16 File")
	doTest(file)

	cleanup(fileBackedFAT, metadata)
}

func TestSetSize(t *testing.T) {
	doTest := func(fat32, isDir, isRoot bool) {
		// Set up filesystem (we do abnormal truncation in this test, so we recerate the filesystem
		// for each test case).
		var fileBackedFAT *testutil.FileFAT
		var metadata *Metadata
		if fat32 {
			fileBackedFAT, metadata = setupFAT32(t, "1G", false)
		} else {
			fileBackedFAT, metadata = setupFAT16(t, "10M", false)
		}
		defer cleanup(fileBackedFAT, metadata)

		// Create the target node
		var n Node
		if isRoot {
			n = checkedMakeRoot(t, metadata, fat32)
		} else {
			r := checkedMakeRoot(t, metadata, fat32)
			if isDir {
				n = checkedMakeDirectoryNode(t, metadata, r, 0)
			} else {
				n = checkedMakeFileNode(t, metadata, r, 0)
			}
		}

		// We want to test everything EXCEPT FAT-16's root, which does not use clusters
		nodeUsesClusters := fat32 || !isRoot
		// Normal directories should never have a size set to "zero", since they will hold "." and
		// ".." entries. This would cause a panic.
		canHaveZeroSize := !isDir || isRoot

		numClustersStart := 3
		buf := testutil.MakeRandomBuffer(int(metadata.Br.ClusterSize()) * numClustersStart)
		if _, err := n.writeAt(buf, 0); err != nil {
			t.Fatal(err)
		} else if nodeUsesClusters && n.NumClusters() != numClustersStart {
			t.Fatal("Unexpected number of starting clusters")
		}
		// Adjust the size of the node (trim down to one cluster)
		newLen := int64(metadata.Br.ClusterSize())
		n.SetSize(newLen)

		if nodeUsesClusters && n.NumClusters() != 1 {
			t.Fatal("Modifying the size should have reduced the number of clusters, but it didn't")
		}

		if canHaveZeroSize {
			n.SetSize(0)
			if nodeUsesClusters {
				if fat32 && isRoot {
					if n.NumClusters() != 1 {
						t.Fatal("Truncating to zero should have left the file with a single cluster")
					}
				} else if n.NumClusters() != 0 {
					t.Fatal("Truncating to zero should have left the file with no clusters")
				}
			}
		}
	}

	glog.Info("Testing FAT32 Root")
	doTest( /* fat32= */ true /* isDir= */, true /* isRoot= */, true)
	glog.Info("Testing FAT32 Directory")
	doTest( /* fat32= */ true /* isDir= */, true /* isRoot= */, false)
	glog.Info("Testing FAT32 File")
	doTest( /* fat32= */ true /* isDir= */, false /* isRoot= */, false)

	glog.Info("Testing FAT16 Root")
	doTest( /* fat32= */ false /* isDir= */, true /* isRoot= */, true)
	glog.Info("Testing FAT16 Directory")
	doTest( /* fat32= */ false /* isDir= */, true /* isRoot= */, false)
	glog.Info("Testing FAT16 File")
	doTest( /* fat32= */ false /* isDir= */, false /* isRoot= */, false)
}

func TestSingleNodeRefs(t *testing.T) {
	doTest := func(n Node) {
		// First, write to the node so it has at least one cluster
		buf := testutil.MakeRandomBuffer(100)
		if _, err := n.writeAt(buf, 0); err != nil {
			t.Fatal(nil)
		}

		// Increment the refcount
		refs := n.RefCount()
		n.RefUp()
		n.RefUp()
		refs += 2
		if n.RefCount() != refs {
			t.Fatalf("Unexpected number of refs: %d (expected %d)", n.RefCount(), refs)
		}

		// Decrement the refcount to one
		if err := n.RefDown(refs - 1); err != nil {
			t.Fatal(err)
		} else if n.RefCount() != 1 {
			t.Fatalf("Unexpected number of refs: %d (expected %d)", n.RefCount(), 1)
		}

		// Decrement the refcount to zero
		if err := n.RefDown(1); err != nil {
			t.Fatal(err)
		} else if n.RefCount() != 0 {
			t.Fatalf("Unexpected number of refs: %d (expected %d)", n.RefCount(), 0)
		}

		// Increment the refcount up from zero
		n.RefUp()
		if n.RefCount() != 1 {
			t.Fatalf("Unexpected number of refs: %d (expected %d)", n.RefCount(), 1)
		}

		// Mark deleted, decrement to zero again
		dir, isDir := n.(DirectoryNode)
		if !isDir || !dir.IsRoot() { // We can't delete the root directory -- don't even try.
			n.MarkDeleted()
			if err := n.RefDown(1); err != nil {
				t.Fatal(err)
			}
		}
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)

	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	glog.Info("Testing FAT32 Root")
	doTest(root)
	dir := checkedMakeDirectoryNode(t, metadata, root, 0)
	glog.Info("Testing FAT32 Directory")
	doTest(dir)
	file := checkedMakeFileNode(t, metadata, root, 1)
	glog.Info("Testing FAT32 File")
	doTest(file)

	cleanup(fileBackedFAT, metadata)
	fileBackedFAT, metadata = setupFAT16(t, "10M", false)

	root = checkedMakeRoot(t, metadata /* fat32= */, false)
	glog.Info("Testing FAT16 Root")
	doTest(root)
	dir = checkedMakeDirectoryNode(t, metadata, root, 0)
	glog.Info("Testing FAT16 Directory")
	doTest(dir)
	file = checkedMakeFileNode(t, metadata, root, 1)
	glog.Info("Testing FAT16 File")
	doTest(file)

	cleanup(fileBackedFAT, metadata)
}

func TestNodeHierarchy(t *testing.T) {
	doTest := func(metadata *Metadata, fat32 bool) {
		// Set up the following hierarchy:
		// /
		// /foo/
		// /foo/foofile.txt
		// /bar/
		// /bar/baz/
		// /bar/bazfile.txt

		root := checkedMakeRoot(t, metadata, fat32)
		foo := checkedMakeDirectoryNode(t, metadata, root, 0)
		foofile := checkedMakeFileNode(t, metadata, foo, 0)
		bar := checkedMakeDirectoryNode(t, metadata, root, 1)
		baz := checkedMakeDirectoryNode(t, metadata, bar, 0)
		bazfile := checkedMakeFileNode(t, metadata, bar, 1)

		contains := func(children []FileNode, target Node) bool {
			for _, child := range children {
				if child == target {
					return true
				}
			}
			return false
		}

		// Verify the children
		if children := root.ChildFiles(); len(children) != 0 {
			t.Fatal("Unexpected number of children")
		}
		if children := foo.ChildFiles(); len(children) != 1 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, foofile) {
			t.Fatal("foofile is not a child of foo")
		}
		if children := bar.ChildFiles(); len(children) != 1 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, bazfile) {
			t.Fatal("bazfile is not a child of bar")
		}
		if children := baz.ChildFiles(); len(children) != 0 {
			t.Fatal("Unexpected number of children")
		}

		// Move the 'foofile' directory inside the root directory
		foofile.MoveFile(root, 1)

		// Verify the structure of the filesystem
		if children := root.ChildFiles(); len(children) != 1 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, foofile) {
			t.Fatal("foofile is not a child of root")
		}
		if children := foo.ChildFiles(); len(children) != 0 {
			t.Fatal("Unexpected number of children")
		}

		if children := bar.ChildFiles(); len(children) != 1 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, bazfile) {
			t.Fatal("bazfile is not a child of bar")
		}
		if children := baz.ChildFiles(); len(children) != 0 {
			t.Fatal("Unexpected number of children")
		}
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	doTest(metadata /* fat32= */, true)
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "10M", false)
	doTest(metadata /* fat32= */, false)
	cleanup(fileBackedFAT, metadata)
}

// Test that "LockParent" will not return an invalid parent, even when the parent is being
// concurrently altered.
func TestLockParent(t *testing.T) {
	checkParentUntilRemoved := func(done chan bool, f FileNode, d DirectoryNode, dirIndex int) {
		// Loop forever, checking parent...
		for {
			parent, index := f.LockParent()
			if parent == nil {
				// ... until it is removed.
				done <- true
				return
			} else if parent != d {
				t.Fatal("Unexpected parent from LockParent")
			} else if index != dirIndex {
				t.Fatal("Unexpected dirIndex from LockParent")
			} else if c, ok := parent.ChildFile(index); !ok || (c != f) {
				t.Fatal("Unexpected child of parent")
			}
			parent.Unlock()
		}
	}

	doTest := func(metadata *Metadata, fat32 bool) {
		root := checkedMakeRoot(t, metadata, fat32)
		dirIndex := 3
		foo := checkedMakeDirectoryNode(t, metadata, root, 0)
		foofile := checkedMakeFileNode(t, metadata, foo, dirIndex)
		done := make(chan bool)

		// Test that "LockParent" can deal with another thread removing the parent --> child
		// relationship
		go checkParentUntilRemoved(done, foofile, foo, dirIndex)
		foo.Lock()
		foo.RemoveFile(dirIndex)
		foo.Unlock()
		<-done

		// Test that "LockParent" can deal with another thread removing the parent <--> child
		// relationship
		foofile = checkedMakeFileNode(t, metadata, foo, dirIndex)
		go checkParentUntilRemoved(done, foofile, foo, dirIndex)
		foo.Lock()
		foofile.Lock()
		foo.RemoveFile(dirIndex)
		foofile.MarkDeleted()
		foofile.Unlock()
		foo.Unlock()
		<-done
	}

	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	doTest(metadata /* fat32= */, true)
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "10M", false)
	doTest(metadata /* fat32= */, false)
	cleanup(fileBackedFAT, metadata)
}

func TestSetSizeFAT32Invalid(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.SetSize(int64(metadata.Br.ClusterSize() + 1))
	t.Fatal("Expected panic related to setting a node size too large")
}

func TestSetSizeFAT16Invalid(t *testing.T) {
	fileBackedFAT, metadata := setupFAT16(t, "10M", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, false)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.SetSize(100000)
	t.Fatal("Expected panic related to setting a root size too large")
}

func TestRoot32PanicDeleted(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.MarkDeleted()
	t.Fatal("Expected panic")
}

func TestRoot16PanicDeleted(t *testing.T) {
	fileBackedFAT, metadata := setupFAT16(t, "10M", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, false)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.MarkDeleted()
	t.Fatal("Expected panic")
}

func TestNodePanicDeletedTwice(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	foo := checkedMakeDirectoryNode(t, metadata, root, 0)
	foo.MarkDeleted()
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.MarkDeleted()
	t.Fatal("Expected panic")
}

func TestNodePanicRefUpAfterDelete(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	foo := checkedMakeDirectoryNode(t, metadata, root, 0)
	foo.RefUp()
	foo.MarkDeleted()
	if err := foo.RefDown(1); err != nil { // This actually deletes foo
		t.Fatal(err)
	}
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.RefUp()
	t.Fatal("Expected panic")
}

// There is no reason why users of the node package should need to modify the size of deleted nodes.
// As a precaution, panic if incorrect behavior is exhibited.
func TestNodePanicSetSizeAfterDelete(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	foo := checkedMakeFileNode(t, metadata, root, 0)
	foo.MarkDeleted()
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.SetSize(0)
	t.Fatal("Expected panic")
}

// Non-root directories require space for "." and ".." entries.
// If their size could be set to zero, it would become invalid.
func TestNodePanicSetSizeNonRootDirectory(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	dir := checkedMakeDirectoryNode(t, metadata, root, 0)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	dir.SetSize(0)
	t.Fatal("Expected panic")
}

func TestNodePanicRefDown(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	foo := checkedMakeDirectoryNode(t, metadata, root, 0)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.RefDown(1)
	t.Fatal("Expected panic")
}

func TestNodePanicMoveAfterDelete(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, metadata)
	root := checkedMakeRoot(t, metadata /* fat32= */, true)
	foofile := checkedMakeFileNode(t, metadata, root, 0)
	bar := checkedMakeDirectoryNode(t, metadata, root, 1)
	root.RemoveFile(0)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foofile.MoveFile(bar, 0)
	t.Fatal("Expected panic")
}

func TestLargestNode(t *testing.T) {
	doTest := func(metadata *Metadata, maxSize int64, fat32, doRoot, doDirectory bool) {
		var largeNode Node
		root := checkedMakeRoot(t, metadata, fat32)
		if doRoot {
			largeNode = root
		} else if doDirectory {
			largeNode = checkedMakeDirectoryNode(t, metadata, root, 0)
		} else {
			largeNode = checkedMakeFileNode(t, metadata, root, 0)
		}
		fileSize := largeNode.Size()
		bufSize := maxSize / 20 // Write in large chunks
		buf := testutil.MakeRandomBuffer(int(bufSize))

		// Fill the entire node with the randomly generated buffer
		for fileSize != maxSize {
			if bufSize+fileSize > maxSize {
				buf = buf[:maxSize-fileSize]
			}
			n, err := largeNode.writeAt(buf, fileSize)
			if err != nil {
				t.Fatal(err)
			} else if n != len(buf) {
				t.Fatalf("Wrote 0x%x bytes (expected 0x%x)", n, len(buf))
			}
			fileSize = largeNode.Size()
		}

		// Once the node is "full", overflow it with a single byte
		buf = testutil.MakeRandomBuffer(1)
		if n, err := largeNode.writeAt(buf, fileSize); err != ErrNoSpace {
			t.Fatal("Expected ErrNoSpace error, but saw: ", err, " with filesize: ", largeNode.Size())
		} else if n != 0 {
			t.Fatalf("Wrote 0x%x bytes (expected 0x%x)", n, 0)
		} else if largeNode.Size() != maxSize {
			t.Fatalf("Node size changed after failed write to 0x%x", largeNode.Size())
		}

		// Try writing partially under and partially over the max size
		buf = testutil.MakeRandomBuffer(100)
		halfBufSize := int64(len(buf) / 2)
		expectedWriteSize := halfBufSize
		if doRoot || doDirectory {
			// Root / Directory contain metadata; they cannot complete partial writes
			expectedWriteSize = 0
		}
		if n, err := largeNode.writeAt(buf, fileSize-halfBufSize); err != ErrNoSpace {
			t.Fatal("Expected ErrNoSpace error, but saw: ", err)
		} else if n != int(expectedWriteSize) {
			t.Fatalf("Wrote 0x%x bytes (expected 0x%x)", n, expectedWriteSize)
		} else if largeNode.Size() != maxSize {
			t.Fatalf("Node size changed after failed write to 0x%x", largeNode.Size())
		}

	}

	fileBackedFAT, metadata := setupFAT32(t, "5G", false)
	fat32 := true
	doRoot := false
	doDirectory := false
	glog.Info("Testing FAT32 File")
	doTest(metadata, maxSizeFile, fat32, doRoot, doDirectory) // FAT32 file
	doRoot = true
	doDirectory = true
	glog.Info("Testing FAT32 Root")
	doTest(metadata, maxSizeDirectory, fat32, doRoot, doDirectory) // FAT32 root
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT32 Directory")
	doTest(metadata, maxSizeDirectory, fat32, doRoot, doDirectory) // FAT32 non-root directory
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "50M", false)
	fat32 = false
	doRoot = true
	doDirectory = true
	_, numRootEntriesMax := metadata.Br.RootReservedInfo()
	glog.Info("Testing FAT16 Root")
	doTest(metadata, numRootEntriesMax*direntry.DirentrySize, fat32, doRoot, doDirectory) // FAT16 root
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT16 Directory")
	doTest(metadata, maxSizeDirectory, fat32, doRoot, doDirectory) // FAT16 non-root directory
	cleanup(fileBackedFAT, metadata)
}

func TestNoSpace(t *testing.T) {
	doTest := func(metadata *Metadata, fat32, doRoot, doDirectory bool) {
		var largeNode Node
		root := checkedMakeRoot(t, metadata, fat32)
		if doRoot {
			largeNode = root
		} else {
			if doDirectory {
				largeNode = checkedMakeDirectoryNode(t, metadata, root, 0)
			} else {
				largeNode = checkedMakeFileNode(t, metadata, root, 0)
			}
			largeNode.RefUp()
		}
		buf := testutil.MakeRandomBuffer(int(100000))

		for {
			n, err := largeNode.writeAt(buf, largeNode.Size())
			if err == fs.ErrResourceExhausted && n < len(buf) {
				// Only valid exit condition: Using all the space in the filesystem and not
				// completing write
				break
			} else if err != nil {
				t.Fatal(err)
			}
		}

		if !doRoot {
			largeNode.MarkDeleted()
			if err := largeNode.RefDown(1); err != nil {
				t.Fatal(err)
			}
		}
	}

	fileBackedFAT, metadata := setupFAT32(t, "600M", false)
	fat32 := true
	doRoot := false
	doDirectory := false
	glog.Info("Testing FAT32 File")
	doTest(metadata, fat32, doRoot, doDirectory) // FAT32 file
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT32 Directory")
	doTest(metadata, fat32, doRoot, doDirectory) // FAT32 non-root directory
	doRoot = true
	doDirectory = true
	glog.Info("Testing FAT32 Root")
	doTest(metadata, fat32, doRoot, doDirectory) // FAT32 root
	cleanup(fileBackedFAT, metadata)

	fileBackedFAT, metadata = setupFAT16(t, "50M", false)
	fat32 = false
	doRoot = false
	doDirectory = false
	glog.Info("Testing FAT16 File")
	doTest(metadata, fat32, doRoot, doDirectory) // FAT16 file
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT16 Directory")
	doTest(metadata, fat32, doRoot, doDirectory) // FAT16 non-root directory
	doRoot = true
	doDirectory = true
	glog.Info("Testing FAT16 Root")
	doTest(metadata, fat32, doRoot, doDirectory) // FAT16 root

	cleanup(fileBackedFAT, metadata)
}
