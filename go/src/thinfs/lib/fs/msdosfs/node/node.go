// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/direntry"
)

const (
	// This file size cap is a fundamental limit of FAT filesystems
	maxSizeFile = int64(0xFFFFFFFF)

	// All direntries must be indexable by a 16-bit integer (for historical reasons)
	maxSizeDirectory = int64((1 << 16) * direntry.DirentrySize)
)

// node represents a regular FAT node (either a file or a directory).
// A node is shared between all files / directories which have it open.
type node struct {
	metadata  *Metadata
	directory bool

	sync.RWMutex
	// File-Only fields
	parent      DirectoryNode // Parent directory containing the direntry for this node
	direntIndex int           // Direntry index in parent directory
	// Directory-Only fields
	children     map[int]FileNode // Map of "direntIndex" --> "FileNode", if a child is open
	startCluster uint32           // The first cluster of the node. Constant
	// Shared
	clusters   []uint32  // The clusters of the node. Does NOT include EOF / free.
	size       int64     // The size of the node.
	references int       // The number of open files/directories referencing this node.
	deleted    bool      // "true" if this node has been deleted.
	mtime      time.Time // Last modified time of node
}

// NewFile creates a new node representing a file.
func NewFile(m *Metadata, parent DirectoryNode, direntIndex int, startCluster uint32, mtime time.Time) (FileNode, error) {
	n := &node{
		metadata:    m,
		directory:   false,
		parent:      parent,
		direntIndex: direntIndex,
		mtime:       mtime,
		references:  0,
	}

	parent.setChildFile(direntIndex, n)

	var err error
	if n.clusters, err = m.ClusterMgr.ClusterCollect(startCluster); err != nil {
		return nil, err
	}
	return n, nil
}

// NewDirectory makes a new node representing a directory.
func NewDirectory(m *Metadata, startCluster uint32, mtime time.Time) (DirectoryNode, error) {
	n := &node{
		metadata:     m,
		directory:    true,
		children:     make(map[int]FileNode),
		startCluster: startCluster,
		mtime:        mtime,
		references:   0,
	}

	var err error
	// Since FAT directories do not store their size in a direntry, the size must be recalculated
	// from the number of used clusters.
	if n.clusters, err = m.ClusterMgr.ClusterCollect(startCluster); err != nil {
		return nil, err
	}
	n.SetSize(int64(m.Br.ClusterSize()) * int64(len(n.clusters)))
	return n, nil
}

func (n *node) Metadata() *Metadata {
	return n.metadata
}

// SetSize attempts to modify the acceptable in-memory "size" of a node.
// Node sizes can be overestimated up to the nearest cluster.
//
// If 'SetSize' renders some clusters inaccessible, it frees them.
func (n *node) SetSize(size int64) {
	clusterSize := int64(n.metadata.Br.ClusterSize())
	maxSize := clusterSize * int64(len(n.clusters))
	if n.deleted {
		panic("Should not modify size of deleted node")
	} else if maxSize < size {
		panic("Setting size larger than accessible by known clusters")
	} else if !n.IsRoot() && n.IsDirectory() && size <= 0 {
		panic("Should not attempt to set size of non-root directory to less than or equal to zero")
	}
	n.size = size

	numClustersRequired := (size + clusterSize - 1) / clusterSize
	if n.IsRoot() && numClustersRequired == 0 {
		// The root directory requires at least one cluster
		numClustersRequired = 1
	}
	if numClustersRequired < int64(len(n.clusters)) {
		// Reduce clusters to the required size
		clusterToDelete := n.clusters[numClustersRequired]
		n.clusters = n.clusters[:numClustersRequired]
		// Make a best-effort attempt to delete the clusters.
		// If an I/O error occurs, clusters may be lost until FAT fsck executes, but the filesystem
		// will not actually be corrupted.
		if len(n.clusters) > 0 {
			n.metadata.ClusterMgr.ClusterTruncate(n.clusters[len(n.clusters)-1])
		} else {
			n.metadata.ClusterMgr.ClusterDelete(clusterToDelete)
		}
	}
}

// MarkDeleted sets the node to be deleted, so it can be removed from disk once fully closed.
func (n *node) MarkDeleted() {
	if n.IsRoot() {
		panic("Cannot delete root")
	} else if n.deleted {
		panic("Node marked as deleted twice")
	}

	n.deleted = true
	n.parent = nil
}

func (n *node) RefUp() {
	if n.references == 0 && n.deleted {
		panic("Cannot acquire reference to fully deleted node")
	}
	n.references++
}

func (n *node) RefCount() int {
	return n.references
}

// RefDown is used to decrement a reference to a node. It can delete the node if it is unlinked
// and the reference count is zero.
//
// A file with zero references can be deleted with the following:
//   n.MarkDeleted()
//   n.RefDown(0)
func (n *node) RefDown(numRefs int) error {
	n.references -= numRefs
	if n.references < 0 {
		panic("Invalid internal refcounting")
	} else if n.references == 0 && n.deleted && len(n.clusters) > 0 {
		return n.Metadata().ClusterMgr.ClusterDelete(n.clusters[0])
	}
	return nil
}

func (n *node) ReadAt(buf []byte, off int64) (int, error) {
	return n.readAt(buf, off)
}

// readAt reads len(buf) bytes from offset "off" in the node into buf.
// It returns the number of bytes read, and an error if the number of bytes read is less than
// len(buf).
func (n *node) readAt(buf []byte, off int64) (int, error) {
	// Short-circuit the cases where offset is reading out of bounds
	if off < 0 || n.size <= off {
		return 0, ErrBadArgument
	}
	clusterSize := int64(n.metadata.Br.ClusterSize())
	bytesRead := 0
	stopReading := false

	for bytesRead < len(buf) && !stopReading {
		clusterIndex := off / clusterSize // Which cluster are we reading from?
		clusterStart := off % clusterSize // How far into the cluster are we starting from?
		if int(clusterIndex) >= len(n.clusters) {
			// Reading at this offset attempts to access a cluster which has not been allocated.
			break
		}
		deviceOffset := int64(n.metadata.Br.ClusterLocationData(n.clusters[clusterIndex])) + clusterStart

		// What's the maximum number of bytes we could read from this single cluster?
		clusterBytesToRead := int(clusterSize - clusterStart)
		if bytesRead+clusterBytesToRead > len(buf) {
			// If the size of the output buffer can't cover this whole cluster, read less.
			clusterBytesToRead = len(buf) - bytesRead
			stopReading = true
		}
		if off+int64(clusterBytesToRead) > n.size {
			// If the size of the node doesn't cover the whole cluster, read less.
			clusterBytesToRead = int(n.size - off)
			stopReading = true
		}

		dBytesRead, err := n.metadata.Dev.ReadAt(buf[bytesRead:bytesRead+clusterBytesToRead], deviceOffset)
		bytesRead += dBytesRead
		off += int64(dBytesRead)
		if err != nil {
			return bytesRead, err
		}
	}
	if bytesRead < len(buf) {
		return bytesRead, ErrEOF
	}
	return bytesRead, nil
}

func (n *node) WriteAt(buf []byte, off int64) (int, error) {
	return n.writeAt(buf, off)
}

func (n *node) writeAt(buf []byte, off int64) (int, error) {
	if n.metadata.Readonly {
		return 0, fs.ErrPermission
	} else if off < 0 {
		return 0, ErrBadArgument
	}

	// If (and only if) the write increases the size of the file, this variable holds the new size.
	maxPotentialSize := off + int64(len(buf))
	maxNodeSize := maxSizeFile
	if n.IsDirectory() {
		maxNodeSize = maxSizeDirectory
	}
	writeBuf := buf

	// Ensure the write does not expand beyond the maximum allowable file / directory size. This
	// should later result in an error, since the input buffer will not be written fully.
	if maxPotentialSize > maxNodeSize {
		if n.directory {
			// Directories are metadata -- partial writes should be avoided
			return 0, ErrNoSpace
		}
		writeBuf = writeBuf[:len(writeBuf)-int(maxPotentialSize-maxNodeSize)]
		maxPotentialSize = maxNodeSize
	}

	// Expand the number of clusters to fill the last byte of the buffer.
	clusterSize := int64(n.metadata.Br.ClusterSize())
	for maxPotentialSize > clusterSize*int64(len(n.clusters)) {
		// Only expand the tail if there is one. Empty files, for example, have no clusters.
		tail := uint32(0)
		if len(n.clusters) > 0 {
			tail = n.clusters[len(n.clusters)-1]
		}
		newCluster, err := n.metadata.ClusterMgr.ClusterExtend(tail)
		if err != nil {
			if n.directory {
				return 0, err
			}
			// Error intentionally ignored; we are just cleaning up after the ClusterExtend error.
			// The writeBuffer cannot be fully written -- instead, only write the chunk which will
			// fit given our constrained cluster size.
			writeBuf = writeBuf[:len(writeBuf)-int(maxPotentialSize-int64(clusterSize)*int64(len(n.clusters)))]
			break
		}
		n.clusters = append(n.clusters, newCluster)
	}

	// Actually write bytes from the input buffer to the allocated clusters.
	bytesWritten := 0
	for bytesWritten < len(writeBuf) {
		clusterIndex := off / clusterSize // Which cluster are we writing to?
		clusterStart := off % clusterSize // How far into the cluster are we starting from?
		deviceOffset := n.metadata.Br.ClusterLocationData(n.clusters[clusterIndex]) + clusterStart

		// What's the maximum number of bytes we could write to this single cluster?
		clusterBytesToWrite := int(clusterSize - clusterStart)
		if bytesWritten+clusterBytesToWrite > len(writeBuf) {
			// If the size of the output buffer can't cover this whole cluster, write less.
			clusterBytesToWrite = len(writeBuf) - bytesWritten
		}
		dBytesWritten, err := n.metadata.Dev.WriteAt(writeBuf[bytesWritten:bytesWritten+clusterBytesToWrite], deviceOffset)
		bytesWritten += dBytesWritten
		off += int64(dBytesWritten)

		// Adjust the size of the node if we have extended it.
		if off > n.size {
			n.size = off
		}
		if err != nil {
			if bytesWritten > 0 {
				n.mtime = time.Now()
			}
			return bytesWritten, err
		}
	}

	if bytesWritten > 0 {
		n.mtime = time.Now()
	}

	// We successfully wrote as many bytes as possible, but the request was still for an unmanagable
	// amount of space.
	if len(writeBuf) != len(buf) {
		return bytesWritten, ErrNoSpace
	}
	return bytesWritten, nil
}

func (n *node) IsDirectory() bool {
	return n.directory
}

func (n *node) StartCluster() uint32 {
	if len(n.clusters) == 0 {
		return n.metadata.ClusterMgr.ClusterEOF()
	}
	return n.clusters[0]
}

func (n *node) ID() uint32 {
	if !n.IsDirectory() {
		panic("Non-directory nodes do not have IDs")
	}
	return n.startCluster
}

func (n *node) NumClusters() int {
	return len(n.clusters)
}

func (n *node) IsRoot() bool {
	// The root can only be a 'normal' node on FAT32
	if n.metadata.Br.Type() != bootrecord.FAT32 {
		return false
	} else if !n.IsDirectory() {
		return false
	} else if n.ID() == n.metadata.Br.RootCluster() {
		return true
	}
	return false
}

func (n *node) Size() int64 {
	return n.size
}

func (n *node) MTime() time.Time {
	return n.mtime
}

func (n *node) SetMTime(mtime time.Time) {
	n.mtime = mtime
}

func (n *node) LockParent() (DirectoryNode, int) {
	n.Lock()
	parent := n.parent
	index := n.direntIndex
	n.Unlock()

	if parent != nil {
		parent.Lock() // Check that 'parent' is still actually our parent
		if n2, ok := parent.ChildFile(index); ok && n == n2 {
			// Intentionally keep the lock on 'parent'
			return parent, index
		}
		parent.Unlock() // We were unlinked
		return nil, 0
	}
	return nil, 0 // No known parent
}

func (n *node) Parent() (DirectoryNode, int) {
	return n.parent, n.direntIndex
}

func (n *node) setChildFile(direntIndex int, child FileNode) {
	if !n.IsDirectory() {
		panic("Files cannot have children")
	} else if _, ok := n.children[direntIndex]; ok {
		panic("setChildFile failed; a child already exists at this index")
	}
	n.children[direntIndex] = child
}

func (n *node) MoveFile(newParent DirectoryNode, newDirentIndex int) {
	if n.parent == nil || newParent == nil {
		panic("Cannot move a node with invalid parents (as either src or dst)")
	}
	// Remove node from old parent
	n.parent.RemoveFile(n.direntIndex)
	// Update node with information regarding new parent
	n.parent = newParent
	n.direntIndex = newDirentIndex
	// Update new parent with information about node
	newParent.setChildFile(newDirentIndex, n)
}

func (n *node) ChildFiles() []FileNode {
	children := make([]FileNode, len(n.children))
	i := 0
	for k := range n.children {
		children[i] = n.children[k]
		i++
	}
	return children
}

func (n *node) ChildFile(direntIndex int) (FileNode, bool) {
	c, ok := n.children[direntIndex]
	return c, ok
}

func (n *node) RemoveFile(direntIndex int) {
	if _, ok := n.children[direntIndex]; !ok {
		panic("Child does not exist in node")
	}
	delete(n.children, direntIndex)
}
