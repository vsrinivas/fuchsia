// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"sync"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/metadata"
)

type root struct {
	info        *metadata.Info
	offsetStart int64 // Device offset at which the root starts.
	maxSize     int64 // The maximum size of the root node.

	mu         sync.RWMutex
	children   map[uint]Node // Map of "direntIndex" --> "Node", if a child is open.
	size       int64         // The size of the node.
	references int           // The number of open files referencing this node.
}

// NewRoot makes a new root, specific for FAT-12 / FAT-16
func NewRoot(info *metadata.Info, offsetStart, maxSize int64) Node {
	r := &root{
		info:        info,
		offsetStart: offsetStart,
		maxSize:     maxSize,
		children:    make(map[uint]Node),
		size:        maxSize,
		references:  1,
	}

	return r
}

func (r *root) Info() *metadata.Info {
	return r.info
}

func (r *root) SetSize(size int64) error {
	if r.maxSize < size {
		return ErrNoSpace
	}
	r.size = size
	return nil
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

func (r *root) ReadAt(buf []byte, off int64) (int, error) {
	bytesToRead := len(buf)

	if off < 0 {
		return 0, ErrBadArgument
	} else if off >= r.size {
		// Would the start of this read extend beyond the end of root? If so, don't read.
		bytesToRead = 0
	} else if off+int64(bytesToRead) > r.size {
		// Would the end of this read extend beyond the end of root? If so, read less.
		bytesToRead = int(r.size - off)
	}

	bytesRead, err := r.info.Dev.ReadAt(buf[:bytesToRead], r.offsetStart+off)
	if err != nil {
		return 0, err
	}

	if bytesRead < len(buf) {
		return bytesRead, ErrEOF
	}
	return bytesRead, nil
}

func (r *root) WriteAt(buf []byte, off int64) (int, error) {
	if r.info.Readonly {
		return 0, fs.ErrReadOnly
	}

	bytesToWrite := len(buf)
	if off < 0 {
		return 0, ErrBadArgument
	} else if off >= r.maxSize {
		// Would the start of this write extend beyond the max size? If so, don't write.
		bytesToWrite = 0
	} else if off+int64(bytesToWrite) > r.maxSize {
		// Would the end of this write extend beyond the max size? If so, write less.
		bytesToWrite = int(r.maxSize - off)
	}
	bytesWritten, err := r.info.Dev.WriteAt(buf[:bytesToWrite], r.offsetStart+off)

	// Adjust the size of the root if we have extended it.
	if off+int64(bytesWritten) > r.size {
		r.size = off + int64(bytesWritten)
	}

	if err != nil {
		return bytesWritten, err
	}

	// We wrote as many bytes as possible, but there was not enough space for that many bytes.
	if bytesWritten < len(buf) {
		return bytesWritten, ErrNoSpace
	}
	return bytesWritten, nil
}

func (r *root) IsDirectory() bool {
	return true
}

// This root does not use clusters. However, for the sake of simplicity, we'll simply lie using a
// reserved cluster.
func (r *root) StartCluster() uint32 {
	return r.info.ClusterMgr.ClusterEOF()
}

func (r *root) NumClusters() int {
	return 0
}

func (r *root) IsRoot() bool {
	return true
}

func (r *root) Size() int64 {
	return r.size
}

func (r *root) Parent() (parent Node, direntIndex uint) {
	return nil, 0
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

func (r *root) Children() []Node {
	children := make([]Node, len(r.children))
	i := 0
	for k := range r.children {
		children[i] = r.children[k]
		i++
	}
	return children
}

func (r *root) Child(direntIndex uint) (Node, bool) {
	c, ok := r.children[direntIndex]
	return c, ok
}

func (r *root) setChild(direntIndex uint, child Node) {
	if _, ok := r.children[direntIndex]; ok {
		panic("setChild failed; a child already exists at this index")
	}
	r.children[direntIndex] = child
}

func (r *root) MoveNode(newParent Node, newDirentIndex uint) {
	panic("Cannot move root16")
}

func (r *root) RemoveChild(direntIndex uint) {
	if _, ok := r.children[direntIndex]; !ok {
		panic("Child does not exist in root16")
	}
	delete(r.children, direntIndex)
}
