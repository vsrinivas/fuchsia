// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/fs"
)

var (
	// ErrNoSpace indicates the requested operation requires more space than is available
	ErrNoSpace = fs.ErrNoSpace

	// ErrBadArgument indicates the argument passed to a node was invalid
	ErrBadArgument = fs.ErrInvalidArgs
)

// FileNode implements the interface of a file (leaf node) in the FAT filesystem
type FileNode interface {
	Node
	WriteAt(p []byte, off int64) (int, error) // Implements io.WriterAt
	ReadAt(p []byte, off int64) (int, error)  // Implements io.ReaderAt

	MoveFile(newParent DirectoryNode, newDirentIndex int) // Relocate a file node to a new directory
	LockParent() (parent DirectoryNode, direntIndex int)  // Return the parent directory (locked, if it exists) for this file
	Parent() (parent DirectoryNode, direntIndex int)      // Return the parent directory for this file (NOT thread-safe)
}

// DirectoryNode implements the interface of a directory in the FAT filesystem
type DirectoryNode interface {
	Node

	RemoveFile(direntIndex int)                 // Remove a child file open in this directory
	ChildFiles() []FileNode                     // Return the child files open in this directory
	ChildFile(direntIndex int) (FileNode, bool) // Return a child file open at a particular direntIndex

	// Methods which do not require lock
	IsRoot() bool // True iff the node corresponds to the root of a filesystem
	ID() uint32   // Unique ID which identifies directory. Guaranteed to be 0 for root nodes

	setChildFile(direntIndex int, child FileNode) // Internal method to place a child at an empty direntIndex
}

// Node implements the interface of a single node (file, directory, or root) in the FAT filesystem.
type Node interface {
	sync.Locker // Writer-lock access
	RLock()     // Reader-lock access
	RUnlock()   // Unlock reader lock

	// Write-access methods, which may modify the contents of the Node
	SetSize(size int64)        // Change node size. Shrinking can remove clusters
	SetMTime(mtime time.Time)  // Updates the last modified time
	RefUp()                    // Increment refs
	RefDown(numRefs int) error // Decrement refs, possibly delete clusters if they're unused
	MarkDeleted()              // Mark that the node's clusters should be deleted when refs is zero

	// Read-access methods, which do not modify the contents of the Node
	Size() int64          // Return the number of bytes accessible within the node
	StartCluster() uint32 // Returns the first cluster of the node (or EOF)
	NumClusters() int     // Returns the number of clusters used by the node (internally)
	RefCount() int        // Number of external references ('refs') to the node
	MTime() time.Time     // Get last modified time of node, if known
	IsDeleted() bool      // Return if the entry has been deleted

	// Accessible without a lock
	IsDirectory() bool   // True iff the node corresponds to a directory
	Metadata() *Metadata // Return info about the node's filesystem

	// Internal methods
	writeAt(p []byte, off int64) (int, error) // Implements io.WriterAt
	readAt(p []byte, off int64) (int, error)  // Implements io.ReaderAt
}
