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
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/metadata"
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
	info        *metadata.Info
	isDirectory bool

	sync.RWMutex
	parent      Node          // Parent directory containing the direntry for this node.
	direntIndex uint          // Direntry index in parent directory
	children    map[uint]Node // Map of "direntIndex" --> "Node", if a child is open.
	clusters    []uint32      // The starting cluster of the node. Does NOT include EOF / free.
	size        int64         // The size of the node.
	references  int           // The number of open files/directories referencing this node.
	deleted     bool          // "true" if this node has been deleted.
	mtime       time.Time     // Last modified time of node
}

// New makes a new node.
//
// If 'parent' is not nil, then parent.Child(direntIndex) is set to the new node, and node.Parent()
// is set to the parent.
func New(info *metadata.Info, isDirectory bool, parent Node, direntIndex uint, startCluster uint32, mtime time.Time, refs bool) (Node, error) {
	// Node is not open anywhere else -- let's open it now.
	numRefs := 0
	if refs {
		numRefs = 1
	}
	n := &node{
		info:        info,
		isDirectory: isDirectory,
		parent:      parent,
		direntIndex: direntIndex,
		mtime:       mtime,
		references:  numRefs,
	}

	if isDirectory {
		// Lazily make a map of children -- this info is not relevant for files.
		n.children = make(map[uint]Node)
	}

	if parent != nil {
		parent.setChild(direntIndex, n)
	}

	var err error
	n.clusters, err = info.ClusterMgr.ClusterCollect(startCluster)
	if err != nil {
		return nil, err
	}
	return n, nil
}

func (n *node) Info() *metadata.Info {
	return n.info
}

// SetSize attempts to modify the acceptable in-memory "size" of a node.
// Node sizes can be overestimated up to the nearest cluster.
//
// If 'SetSize' renders some clusters inaccessible, it frees them.
func (n *node) SetSize(size int64) error {
	clusterSize := int64(n.info.Br.ClusterSize())
	maxSize := clusterSize * int64(len(n.clusters))
	if n.deleted {
		panic("Should not modify size of deleted node")
	} else if maxSize < size {
		return ErrNoSpace
	} else if !n.IsRoot() && n.isDirectory && size <= 0 {
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
		n.info.ClusterMgr.ClusterDelete(clusterToDelete)
	}
	return nil
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
		return n.Info().ClusterMgr.ClusterDelete(n.clusters[0])
	}
	return nil
}

// ReadAt reads len(buf) bytes from offset "off" in the node into buf.
// It returns the number of bytes read, and an error if the number of bytes read is less than
// len(buf).
func (n *node) ReadAt(buf []byte, off int64) (int, error) {
	// Short-circuit the cases where offset is reading out of bounds
	if off < 0 || n.size <= off {
		return 0, ErrBadArgument
	}
	clusterSize := int64(n.info.Br.ClusterSize())
	bytesRead := 0
	stopReading := false

	for bytesRead < len(buf) && !stopReading {
		clusterIndex := off / clusterSize // Which cluster are we reading from?
		clusterStart := off % clusterSize // How far into the cluster are we starting from?
		if int(clusterIndex) >= len(n.clusters) {
			// Reading at this offset attempts to access a cluster which has not been allocated.
			break
		}
		deviceOffset := int64(n.info.Br.ClusterLocationData(n.clusters[clusterIndex])) + clusterStart

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

		dBytesRead, err := n.info.Dev.ReadAt(buf[bytesRead:bytesRead+clusterBytesToRead], deviceOffset)
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

// WriteAt writes the bytes of buf at offset "off". It returns the number of bytes written, and an
// error if the number of bytes written is less than len(buf).
func (n *node) WriteAt(buf []byte, off int64) (int, error) {
	if n.info.Readonly {
		return 0, fs.ErrPermission
	} else if off < 0 {
		return 0, ErrBadArgument
	}

	// TODO(smklein): Consider bypassing the cache for extremely large writes.

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
		writeBuf = writeBuf[:len(writeBuf)-int(maxPotentialSize-maxNodeSize)]
		maxPotentialSize = maxNodeSize
	}

	// Expand the number of clusters to fill the last byte of the buffer.
	clusterSize := int64(n.info.Br.ClusterSize())
	for maxPotentialSize > int64(clusterSize)*int64(len(n.clusters)) {
		// Only expand the tail if there is one. Empty files, for example, have no clusters.
		tail := uint32(0)
		if len(n.clusters) > 0 {
			tail = n.clusters[len(n.clusters)-1]
		}
		newCluster, err := n.info.ClusterMgr.ClusterExtend(tail)
		if err != nil {
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
		deviceOffset := n.info.Br.ClusterLocationData(n.clusters[clusterIndex]) + clusterStart

		// What's the maximum number of bytes we could write to this single cluster?
		clusterBytesToWrite := int(clusterSize - clusterStart)
		if bytesWritten+clusterBytesToWrite > len(writeBuf) {
			// If the size of the output buffer can't cover this whole cluster, write less.
			clusterBytesToWrite = len(writeBuf) - bytesWritten
		}
		dBytesWritten, err := n.info.Dev.WriteAt(writeBuf[bytesWritten:bytesWritten+clusterBytesToWrite], deviceOffset)
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
	return n.isDirectory
}

func (n *node) StartCluster() uint32 {
	if len(n.clusters) == 0 {
		return n.info.ClusterMgr.ClusterEOF()
	}
	return n.clusters[0]
}

func (n *node) NumClusters() int {
	return len(n.clusters)
}

func (n *node) IsRoot() bool {
	// The root can only be a 'normal' node on FAT32
	if n.info.Br.Type() != bootrecord.FAT32 {
		return false
	}

	if len(n.clusters) > 0 {
		startCluster := n.info.Br.RootCluster()
		if n.clusters[0] == startCluster {
			return true
		}
	}
	return false
}

func (n *node) Size() int64 {
	return n.size
}

func (n *node) MTime() time.Time {
	return n.mtime
}

func (n *node) Parent() (Node, uint) {
	return n.parent, n.direntIndex
}

func (n *node) setChild(direntIndex uint, child Node) {
	if !n.isDirectory {
		panic("Files cannot have children")
	} else if _, ok := n.children[direntIndex]; ok {
		panic("setChild failed; a child already exists at this index")
	}
	n.children[direntIndex] = child
}

func (n *node) MoveNode(newParent Node, newDirentIndex uint) {
	if n.parent == nil || newParent == nil {
		panic("Cannot move a node with invalid parents (as either src or dst)")
	}
	// Remove node from old parent
	n.parent.RemoveChild(n.direntIndex)
	// Update node with information regarding new parent
	n.parent = newParent
	n.direntIndex = newDirentIndex
	// Update new parent with information about node
	newParent.setChild(newDirentIndex, n)
}

func (n *node) Children() []Node {
	children := make([]Node, len(n.children))
	i := 0
	for k := range n.children {
		children[i] = n.children[k]
		i++
	}
	return children
}

func (n *node) Child(direntIndex uint) (Node, bool) {
	c, ok := n.children[direntIndex]
	return c, ok
}

func (n *node) RemoveChild(direntIndex uint) {
	if _, ok := n.children[direntIndex]; !ok {
		panic("Child does not exist in node")
	}
	delete(n.children, direntIndex)
}
