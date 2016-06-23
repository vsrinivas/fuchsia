// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package node

import (
	"bytes"
	"testing"

	"github.com/golang/glog"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/cluster"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/direntry"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/metadata"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/testutil"
	"fuchsia.googlesource.com/thinfs/lib/thinio"
)

func setupFAT32(t *testing.T, size string, readonly bool) (*testutil.FileFAT, *metadata.Info) {
	fileBackedFAT := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
	dev := fileBackedFAT.GetRawDevice()
	info := &metadata.Info{
		Dev:      thinio.NewConductor(dev, 8*1024),
		Readonly: readonly,
	}
	var err error
	info.Br, err = bootrecord.New(info.Dev)
	if err != nil {
		t.Fatal(err)
	} else if info.Br.Type() != bootrecord.FAT32 {
		t.Fatal("FAT created, but it was not FAT32")
	}

	info.ClusterMgr, err = cluster.Mount(info.Dev, info.Br, info.Readonly)
	if err != nil {
		t.Fatal(err)
	}

	return fileBackedFAT, info
}

func setupFAT16(t *testing.T, size string, readonly bool) (*testutil.FileFAT, *metadata.Info) {
	fileBackedFAT := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
	dev := fileBackedFAT.GetRawDevice()
	info := &metadata.Info{
		Dev:      thinio.NewConductor(dev, 8*1024),
		Readonly: readonly,
	}
	var err error
	info.Br, err = bootrecord.New(info.Dev)
	if err != nil {
		t.Fatal(err)
	} else if info.Br.Type() != bootrecord.FAT16 {
		t.Fatal("FAT created, but it was not FAT16")
	}

	info.ClusterMgr, err = cluster.Mount(info.Dev, info.Br, info.Readonly)
	if err != nil {
		t.Fatal(err)
	}

	return fileBackedFAT, info
}

func cleanup(fileBackedFAT *testutil.FileFAT, info *metadata.Info) {
	fileBackedFAT.RmfsFAT()
	info.Dev.Close()
}

func checkedMakeRoot(t *testing.T, info *metadata.Info, fat32 bool) Node {
	if fat32 {
		isDirectory := true
		direntIndex := uint(0)
		startCluster := info.Br.RootCluster()
		root, err := New(info, isDirectory, nil, direntIndex, startCluster, true)
		if err != nil {
			t.Fatal(err)
		} else if root.Info() != info {
			t.Fatal("Invalid info")
		} else if !root.IsDirectory() {
			t.Fatal("Node incorrectly thinks it is not a directory")
		} else if !root.IsRoot() {
			t.Fatal("Node incorrectly thinks it is not root")
		} else if parent, _ := root.Parent(); parent != nil {
			t.Fatal("Root cannot have a parent")
		}
		return root
	}

	// FAT 12 / 16 case:
	offsetStart, numRootEntriesMax := info.Br.RootReservedInfo()
	direntrySize := int64(direntry.DirentrySize)
	root := NewRoot(info, offsetStart, numRootEntriesMax*direntrySize)
	if root.Info() != info {
		t.Fatal("Invalid info")
	} else if !root.IsDirectory() {
		t.Fatal("Node incorrectly thinks it is not a directory")
	} else if !root.IsRoot() {
		t.Fatal("Node incorrectly thinks it is not root")
	} else if parent, _ := root.Parent(); parent != nil {
		t.Fatal("Root cannot have a parent")
	}

	return root
}

func checkedMakeNode(t *testing.T, info *metadata.Info, parent Node, direntIndex uint, isDirectory bool) Node {
	node, err := New(info, isDirectory, parent, direntIndex, 0, true)
	if err != nil {
		t.Fatal(err)
	} else if node.Info() != info {
		t.Fatal("Invalid info")
	} else if node.IsRoot() {
		t.Fatal("Node incorrectly thinks it is root")
	} else if node.IsDirectory() != isDirectory {
		t.Fatal("n.IsDirectory() returned wrong result")
	} else if node.Size() != 0 {
		t.Fatal("node.Size() should be zero")
	} else if node.StartCluster() != info.ClusterMgr.ClusterEOF() {
		t.Fatal("Node should be initialized with an EOF cluster")
	} else if node.NumClusters() != 0 {
		t.Fatal("Node should be initialized with no clusters")
	} else if node.RefCount() != 1 {
		t.Fatal("Node should be initialized with a single reference")
	} else if p, i := node.Parent(); p != parent || i != direntIndex {
		t.Fatal("node.Parent() returned the wrong node / index combo")
	} else if c, exists := parent.Child(direntIndex); c != node || !exists {
		t.Fatal("Child should exist in the parent, but it does not")
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
			if l, err := n.ReadAt(readbuf, 0); err != nil {
				t.Fatal(err)
			} else if l != len(readbuf) {
				t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf))
			} else if !bytes.Equal(readbuf, buf) {
				t.Fatal("Bytes read not equal to input bytes (buf exact)")
			}

			// A read of 'one less byte' should work
			readbuf = make([]byte, len(buf)-1)
			if l, err := n.ReadAt(readbuf, 0); err != nil {
				t.Fatal(err)
			} else if l != len(readbuf) {
				t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf))
			} else if !bytes.Equal(readbuf, buf[:len(buf)-1]) {
				t.Fatal("Bytes read not equal to input bytes (buf minus one byte)")
			}

			// A read of 'one more byte' should work, but also return an EOF error.
			readbuf = make([]byte, len(buf)+1)
			if l, err := n.ReadAt(readbuf, 0); err != ErrEOF {
				t.Fatal("Expected an EOF error")
			} else if l != len(readbuf)-1 {
				t.Fatalf("Unexpected read length: %d (expected %d)", l, len(readbuf)-1)
			} else if !bytes.Equal(readbuf[:len(readbuf)-1], buf) {
				t.Fatal("Bytes read not equal to input bytes (buf plus one byte)")
			}
		}

		if l, err := n.WriteAt(buf1, 0); err != nil {
			t.Fatal(err)
		} else if l != len(buf1) {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, len(buf1))
		} else if n.Size() != int64(len(buf1)) {
			t.Fatalf("Unexpected node size: %d (expected %d)", n.Size(), len(buf1))
		}
		verifyBuffer(buf1)

		if l, err := n.WriteAt(buf2, int64(len(buf1))); err != nil {
			t.Fatal(err)
		} else if l != len(buf2) {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, len(buf2))
		} else if n.Size() != int64(len(bufCombined)) {
			t.Fatalf("Unexpected node size: %d (expected %d)", n.Size(), len(bufCombined))
		}
		verifyBuffer(bufCombined)

		// Adjust the size of the node (trim down to buf1)
		numClusters := n.NumClusters()
		if err := n.SetSize(int64(len(buf1))); err != nil {
			t.Fatal(err)
		} else if n.NumClusters() != numClusters {
			t.Fatal("Modifying the size shouldn't change the underlying cluster count, but it did")
		}
		verifyBuffer(buf1)

		// Adjust the size back to the 'actual' size
		if err := n.SetSize(int64(len(bufCombined))); err != nil {
			t.Fatal(err)
		}
		verifyBuffer(bufCombined)

		// This is somewhat cheating, but force the filesystem to become readonly
		n.Info().Readonly = true
		// We can still read
		verifyBuffer(bufCombined)
		// We cannot write
		if _, err := n.WriteAt(buf1, 0); err != fs.ErrReadOnly {
			t.Fatal("Expected ReadOnly error")
		}
		n.Info().Readonly = false

		// TEST: Edge cases of reading / writing

		// A large read should fail with EOF; it's out of bounds
		readbuf := make([]byte, 1)
		largeOffset := int64(len(bufCombined) + 10)
		if l, err := n.ReadAt(readbuf, largeOffset); err != ErrEOF {
			t.Fatal("Expected an EOF error")
		} else if l != 0 {
			t.Fatalf("Unexpected read length: %d (expected %d)", l, 0)
		}

		// A large write can succeed; it will just force the file to allocate clusters
		buf := []byte{'a'}
		readbuf = make([]byte, 1)
		if l, err := n.WriteAt(buf, largeOffset); err != nil {
			t.Fatal(err)
		} else if l != 1 {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, 1)
		} else if _, err := n.ReadAt(readbuf, largeOffset); err != nil {
			t.Fatal(err)
		} else if !bytes.Equal(buf, readbuf) {
			t.Fatal("Read buffer did not equal write buffer")
		}

		// Test negative writes / reads
		if l, err := n.WriteAt(buf, -1); err != ErrBadArgument {
			t.Fatal("Expected ErrBadArgument")
		} else if l != 0 {
			t.Fatalf("Unexpected write length: %d (expected %d)", l, 0)
		}
		if l, err := n.ReadAt(buf, -1); err != ErrBadArgument {
			t.Fatal("Expected ErrBadArgument")
		} else if l != 0 {
			t.Fatalf("Unexpected read length: %d (expected %d)", l, 0)
		}

		// Test empty writes / reads
		var emptybuf []byte
		if n, err := n.WriteAt(emptybuf, 0); err != nil {
			t.Fatal(err)
		} else if n != 0 {
			t.Fatalf("Empty write actually wrote %d bytes", n)
		}
		if n, err := n.ReadAt(emptybuf, 0); err != nil {
			t.Fatal(err)
		} else if n != 0 {
			t.Fatalf("Empty read actually read %d bytes", n)
		}
	}

	fileBackedFAT, info := setupFAT32(t, "1G", false)

	root := checkedMakeRoot(t, info /* fat32= */, true)
	glog.Info("Testing FAT32 Root")
	doTest(root)
	dir := checkedMakeNode(t, info, root, 0 /* isDirectory= */, true)
	glog.Info("Testing FAT32 Directory")
	doTest(dir)
	file := checkedMakeNode(t, info, root, 1 /* isDirectory= */, false)
	glog.Info("Testing FAT32 File")
	doTest(file)

	cleanup(fileBackedFAT, info)
	fileBackedFAT, info = setupFAT16(t, "10M", false)

	root = checkedMakeRoot(t, info /* fat32= */, false)
	// Root16 starts at 'max size', we need to manually change it to match the format of this test
	if err := root.SetSize(0); err != nil {
		t.Fatal(err)
	}
	glog.Info("Testing FAT16 Root")
	doTest(root)
	dir = checkedMakeNode(t, info, root, 0 /* isDirectory= */, true)
	glog.Info("Testing FAT16 Directory")
	doTest(dir)
	file = checkedMakeNode(t, info, root, 1 /* isDirectory= */, false)
	glog.Info("Testing FAT16 File")
	doTest(file)

	cleanup(fileBackedFAT, info)
}

func TestSingleNodeRefs(t *testing.T) {
	doTest := func(n Node) {
		// First, write to the node so it has at least one cluster
		buf := testutil.MakeRandomBuffer(100)
		if _, err := n.WriteAt(buf, 0); err != nil {
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
		if !n.IsRoot() { // We can't delete the root directory -- don't even try.
			n.MarkDeleted()
			if err := n.RefDown(1); err != nil {
				t.Fatal(err)
			}
		}
	}

	fileBackedFAT, info := setupFAT32(t, "1G", false)

	root := checkedMakeRoot(t, info /* fat32= */, true)
	glog.Info("Testing FAT32 Root")
	doTest(root)
	dir := checkedMakeNode(t, info, root, 0 /* isDirectory= */, true)
	glog.Info("Testing FAT32 Directory")
	doTest(dir)
	file := checkedMakeNode(t, info, root, 1 /* isDirectory= */, false)
	glog.Info("Testing FAT32 File")
	doTest(file)

	cleanup(fileBackedFAT, info)
	fileBackedFAT, info = setupFAT16(t, "10M", false)

	root = checkedMakeRoot(t, info /* fat32= */, false)
	glog.Info("Testing FAT16 Root")
	doTest(root)
	dir = checkedMakeNode(t, info, root, 0 /* isDirectory= */, true)
	glog.Info("Testing FAT16 Directory")
	doTest(dir)
	file = checkedMakeNode(t, info, root, 1 /* isDirectory= */, false)
	glog.Info("Testing FAT16 File")
	doTest(file)

	cleanup(fileBackedFAT, info)
}

func TestNodeHierarchy(t *testing.T) {
	doTest := func(info *metadata.Info, fat32 bool) {
		// Set up the following hierarchy:
		// /
		// /foo/
		// /foo/foofile.txt
		// /bar/
		// /bar/baz/
		// /bar/bazfile.txt

		root := checkedMakeRoot(t, info, fat32)
		foo := checkedMakeNode(t, info, root, 0, true)
		foofile := checkedMakeNode(t, info, foo, 0, false)
		bar := checkedMakeNode(t, info, root, 1, true)
		baz := checkedMakeNode(t, info, bar, 0, true)
		bazfile := checkedMakeNode(t, info, bar, 1, false)

		contains := func(children []Node, target Node) bool {
			for _, child := range children {
				if child == target {
					return true
				}
			}
			return false
		}

		// Verify the children
		if children := root.Children(); len(children) != 2 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, foo) {
			t.Fatal("foo is not a child of root")
		} else if !contains(children, bar) {
			t.Fatal("bar is not a child of root")
		}
		if children := foo.Children(); len(children) != 1 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, foofile) {
			t.Fatal("foofile is not a child of foo")
		}
		if children := bar.Children(); len(children) != 2 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, baz) {
			t.Fatal("baz is not a child of bar")
		} else if !contains(children, bazfile) {
			t.Fatal("bazfile is not a child of bar")
		}
		if children := baz.Children(); len(children) != 0 {
			t.Fatal("Unexpected number of children")
		}

		// Move the 'bar' directory inside the 'foo' directory
		bar.MoveNode(foo, 1)

		// Verify the structure of the filesystem
		if children := root.Children(); len(children) != 1 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, foo) {
			t.Fatal("foo is not a child of root")
		}
		if children := foo.Children(); len(children) != 2 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, foofile) {
			t.Fatal("foofile is not a child of foo")
		} else if !contains(children, bar) {
			t.Fatal("bar is not a child of foo")
		}

		if children := bar.Children(); len(children) != 2 {
			t.Fatal("Unexpected number of children")
		} else if !contains(children, baz) {
			t.Fatal("baz is not a child of bar")
		} else if !contains(children, bazfile) {
			t.Fatal("bazfile is not a child of bar")
		}
		if children := baz.Children(); len(children) != 0 {
			t.Fatal("Unexpected number of children")
		}
	}

	fileBackedFAT, info := setupFAT32(t, "1G", false)
	doTest(info /* fat32= */, true)
	cleanup(fileBackedFAT, info)

	fileBackedFAT, info = setupFAT16(t, "10M", false)
	doTest(info /* fat32= */, false)
	cleanup(fileBackedFAT, info)
}

func TestSetSizeInvalid(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	if err := root.SetSize(int64(info.Br.ClusterSize() + 1)); err != ErrNoSpace {
		t.Fatal("Expected error: Not enough clusters for size")
	}
	cleanup(fileBackedFAT, info)

	fileBackedFAT, info = setupFAT16(t, "10M", false)
	root = checkedMakeRoot(t, info /* fat32= */, false)
	if err := root.SetSize(100000); err != ErrNoSpace {
		t.Fatal("Expected error: Not enough clusters for size")
	}
	cleanup(fileBackedFAT, info)
}

func TestRoot32PanicDeleted(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.MarkDeleted()
	t.Fatal("Expected panic")
}

func TestRoot16PanicDeleted(t *testing.T) {
	fileBackedFAT, info := setupFAT16(t, "10M", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, false)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.MarkDeleted()
	t.Fatal("Expected panic")
}

func TestRoot32PanicMove(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "10G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.MoveNode(foo, 0)
	t.Fatal("Expected panic")
}

func TestRoot16PanicMove(t *testing.T) {
	fileBackedFAT, info := setupFAT16(t, "10M", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, false)
	foo := checkedMakeNode(t, info, root, 0, true)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	root.MoveNode(foo, 0)
	t.Fatal("Expected panic")
}

func TestNodePanicDeletedTwice(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
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
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
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

func TestNodePanicSetSizeAfterDelete(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
	foo.MarkDeleted()
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.SetSize(0)
	t.Fatal("Expected panic")
}

func TestNodePanicRefDown(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
	foo.RefDown(1)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.RefDown(1)
	t.Fatal("Expected panic")
}

func TestNodePanicMoveAfterDelete(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
	bar := checkedMakeNode(t, info, root, 1, true)
	root.RemoveChild(0)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.MoveNode(bar, 0)
	t.Fatal("Expected panic")
}

func TestNodePanicMoveToFile(t *testing.T) {
	fileBackedFAT, info := setupFAT32(t, "1G", false)
	defer cleanup(fileBackedFAT, info)
	root := checkedMakeRoot(t, info /* fat32= */, true)
	foo := checkedMakeNode(t, info, root, 0, true)
	barFile := checkedMakeNode(t, info, root, 1, false)
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("Recover returned without a panic")
		}
	}()
	foo.MoveNode(barFile, 0)
	t.Fatal("Expected panic")
}

func TestLargestNode(t *testing.T) {
	doTest := func(info *metadata.Info, maxSize int64, fat32, doRoot, doDirectory bool) {
		var largeNode Node
		root := checkedMakeRoot(t, info, fat32)
		if doRoot {
			largeNode = root
		} else {
			largeNode = checkedMakeNode(t, info, root, 0, doDirectory)
		}
		fileSize := largeNode.Size()
		bufSize := maxSize / 20 // Write in large chunks
		buf := testutil.MakeRandomBuffer(int(bufSize))

		// Fill the entire node with the randomly generated buffer
		for fileSize != maxSize {
			if bufSize+fileSize > maxSize {
				buf = buf[:maxSize-fileSize]
			}
			n, err := largeNode.WriteAt(buf, fileSize)
			if err != nil {
				t.Fatal(err)
			} else if n != len(buf) {
				t.Fatalf("Wrote 0x%x bytes (expected 0x%x)", n, len(buf))
			}
			fileSize = largeNode.Size()
		}

		// Once the node is "full", overflow it with a single byte
		buf = testutil.MakeRandomBuffer(1)
		if n, err := largeNode.WriteAt(buf, fileSize); err != ErrNoSpace {
			t.Fatal("Expected ErrNoSpace error, but saw: ", err, " with filesize: ", largeNode.Size())
		} else if n != 0 {
			t.Fatalf("Wrote 0x%x bytes (expected 0x%x)", n, 0)
		} else if largeNode.Size() != maxSize {
			t.Fatalf("Node size changed after failed write to 0x%x", largeNode.Size())
		}

		// Try writing partially under and partially over the max size
		buf = testutil.MakeRandomBuffer(100)
		halfBufSize := int64(len(buf) / 2)
		if n, err := largeNode.WriteAt(buf, fileSize-halfBufSize); err != ErrNoSpace {
			t.Fatal("Expected ErrNoSpace error, but saw: ", err)
		} else if n != int(halfBufSize) {
			t.Fatalf("Wrote 0x%x bytes (expected 0x%x)", n, halfBufSize)
		} else if largeNode.Size() != maxSize {
			t.Fatalf("Node size changed after failed write to 0x%x", largeNode.Size())
		}

	}

	fileBackedFAT, info := setupFAT32(t, "5G", false)
	fat32 := true
	doRoot := false
	doDirectory := false
	glog.Info("Testing FAT32 File")
	doTest(info, maxSizeFile, fat32, doRoot, doDirectory) // FAT32 file
	doRoot = true
	doDirectory = true
	glog.Info("Testing FAT32 Root")
	doTest(info, maxSizeDirectory, fat32, doRoot, doDirectory) // FAT32 root
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT32 Directory")
	doTest(info, maxSizeDirectory, fat32, doRoot, doDirectory) // FAT32 non-root directory
	cleanup(fileBackedFAT, info)

	fileBackedFAT, info = setupFAT16(t, "50M", false)
	fat32 = false
	doRoot = true
	doDirectory = true
	_, numRootEntriesMax := info.Br.RootReservedInfo()
	glog.Info("Testing FAT16 Root")
	doTest(info, numRootEntriesMax*direntry.DirentrySize, fat32, doRoot, doDirectory) // FAT16 root
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT16 Directory")
	doTest(info, maxSizeDirectory, fat32, doRoot, doDirectory) // FAT16 non-root directory
	cleanup(fileBackedFAT, info)
}

func TestNoSpace(t *testing.T) {
	doTest := func(info *metadata.Info, fat32, doRoot, doDirectory bool) {
		var largeNode Node
		root := checkedMakeRoot(t, info, fat32)
		if doRoot {
			largeNode = root
		} else {
			largeNode = checkedMakeNode(t, info, root, 0, doDirectory)
		}
		buf := testutil.MakeRandomBuffer(int(100000))

		for {
			n, err := largeNode.WriteAt(buf, largeNode.Size())
			if err == ErrNoSpace && n < len(buf) {
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

	fileBackedFAT, info := setupFAT32(t, "600M", false)
	fat32 := true
	doRoot := false
	doDirectory := false
	glog.Info("Testing FAT32 File")
	doTest(info, fat32, doRoot, doDirectory) // FAT32 file
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT32 Directory")
	doTest(info, fat32, doRoot, doDirectory) // FAT32 non-root directory
	doRoot = true
	doDirectory = true
	glog.Info("Testing FAT32 Root")
	doTest(info, fat32, doRoot, doDirectory) // FAT32 root
	cleanup(fileBackedFAT, info)

	fileBackedFAT, info = setupFAT16(t, "50M", false)
	fat32 = false
	doRoot = false
	doDirectory = false
	glog.Info("Testing FAT16 File")
	doTest(info, fat32, doRoot, doDirectory) // FAT16 file
	doRoot = false
	doDirectory = true
	glog.Info("Testing FAT16 Directory")
	doTest(info, fat32, doRoot, doDirectory) // FAT16 non-root directory
	doRoot = true
	doDirectory = true
	glog.Info("Testing FAT16 Root")
	doTest(info, fat32, doRoot, doDirectory) // FAT16 root

	cleanup(fileBackedFAT, info)
}
