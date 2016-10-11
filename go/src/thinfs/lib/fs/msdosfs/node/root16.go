// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/fs"
)

type root struct {
	metadata    *Metadata
	offsetStart int64 // Device offset at which the root starts.
	maxSize     int64 // The maximum size of the root node.

	mu         sync.RWMutex
	children   map[int]FileNode // Map of "direntIndex" --> "FileNode", if a child is open.
	size       int64            // The size of the node.
	references int              // The number of open files referencing this node.
}

// NewRoot makes a new root, specific for FAT-12 / FAT-16
func NewRoot(m *Metadata, offsetStart, maxSize int64) DirectoryNode {
	r := &root{
		metadata:    m,
		offsetStart: offsetStart,
		maxSize:     maxSize,
		children:    make(map[int]FileNode),
		size:        maxSize,
		references:  1,
	}

	return r
}

func (r *root) Metadata() *Metadata {
	return r.metadata
}

func (r *root) SetSize(size int64) {
	if r.maxSize < size {
		panic("Setting root size to something larger than max size")
	}
	r.size = size
}

func (r *root) MarkDeleted() {
	panic("Cannot delete root")
}

func (r *root) RefUp() {
	r.references++
}

func (r *root) RefCount() int {
	return r.references
}

func (r *root) RefDown(numRefs int) error {
	r.references -= numRefs

	if r.references < 0 {
		panic("Invalid internal refcounting")
	}

	return nil
}

func (r *root) readAt(buf []byte, off int64) (int, error) {
	bytesToRead := len(buf)

	if off < 0 || r.size <= off {
		return 0, ErrBadArgument
	} else if off+int64(bytesToRead) > r.size {
		// Would the end of this read extend beyond the end of root? If so, read less.
		bytesToRead = int(r.size - off)
	}

	bytesRead, err := r.metadata.Dev.ReadAt(buf[:bytesToRead], r.offsetStart+off)
	if err != nil {
		return 0, err
	}

	if bytesRead < len(buf) {
		return bytesRead, ErrEOF
	}
	return bytesRead, nil
}

func (r *root) writeAt(buf []byte, off int64) (int, error) {
	if r.metadata.Readonly {
		return 0, fs.ErrPermission
	} else if off < 0 {
		return 0, ErrBadArgument
	} else if off >= r.maxSize || off+int64(len(buf)) > r.maxSize {
		// Would the start/end of this write extend beyond the max size? If so, don't write.
		return 0, ErrNoSpace
	}

	bytesWritten, err := r.metadata.Dev.WriteAt(buf, r.offsetStart+off)

	// Adjust the size of the root if we have extended it.
	if off+int64(bytesWritten) > r.size {
		r.size = off + int64(bytesWritten)
	}

	return bytesWritten, err
}

func (r *root) IsDirectory() bool {
	return true
}

// This root does not use clusters. However, for the sake of simplicity, we'll simply lie using a
// reserved cluster.
func (r *root) StartCluster() uint32 {
	return r.metadata.ClusterMgr.ClusterEOF()
}

func (r *root) ID() uint32 {
	return 0
}

func (r *root) NumClusters() int {
	return 0
}

func (r *root) IsRoot() bool {
	return true
}

func (r *root) MTime() time.Time {
	return time.Time{}
}

func (r *root) SetMTime(mtime time.Time) {}

func (r *root) Size() int64 {
	return r.size
}

func (r *root) Lock() {
	r.mu.Lock()
}
func (r *root) Unlock() {
	r.mu.Unlock()
}
func (r *root) RLock() {
	r.mu.RLock()
}
func (r *root) RUnlock() {
	r.mu.RUnlock()
}

func (r *root) ChildFiles() []FileNode {
	children := make([]FileNode, len(r.children))
	i := 0
	for k := range r.children {
		children[i] = r.children[k]
		i++
	}
	return children
}

func (r *root) ChildFile(direntIndex int) (FileNode, bool) {
	c, ok := r.children[direntIndex]
	return c, ok
}

func (r *root) setChildFile(direntIndex int, child FileNode) {
	if _, ok := r.children[direntIndex]; ok {
		panic("setChildFile failed; a child already exists at this index")
	}
	r.children[direntIndex] = child
}

func (r *root) RemoveFile(direntIndex int) {
	if _, ok := r.children[direntIndex]; !ok {
		panic("Child does not exist in root16")
	}
	delete(r.children, direntIndex)
}
