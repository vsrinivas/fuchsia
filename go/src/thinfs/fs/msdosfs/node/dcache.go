// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/fs"
)

// Dcache destribes a cache of directories
type Dcache struct {
	sync.Mutex
	entries map[uint32]*dcacheEntry // Map of "Directory ID" --> Directory node
}

// TODO(smklein): By sharding based on keys, this can become much more finely-grained.
// dcacheEntry represents the "value" in the key / value dcache structure
type dcacheEntry struct {
	n    DirectoryNode
	refs int
}

// Init initializes dcache structure
func (d *Dcache) Init() {
	d.entries = make(map[uint32]*dcacheEntry)
}

// AllEntries returns all nodes in the dcache
//
// NOT thread-safe
func (d *Dcache) AllEntries() []DirectoryNode {
	var nodes []DirectoryNode
	for k := range d.entries {
		nodes = append(nodes, d.entries[k].n)
	}
	return nodes
}

// CreateOrAcquire either
// (1) Creates a new node and places it in the dcache (ref = 1), or
// (2) Acquires a node which is already in the dcache (ref++)
// Use this function when access is needed to a Directory which may or may not be open.
//
// Thread-safe
func (d *Dcache) CreateOrAcquire(m *Metadata, id uint32, mtime time.Time) (DirectoryNode, error) {
	d.Lock()
	defer d.Unlock()
	newNode := d.acquire(id)
	if newNode == nil {
		// Add the directory to the dcache if it's not there already
		var err error
		newNode, err = NewDirectory(m, id, mtime)
		if err != nil {
			return nil, err
		}
		d.insert(id, newNode)
	}
	return newNode, nil
}

// Lookup (and acquire) the directory node, if it exists
//
// Thread-safe
func (d *Dcache) Lookup(id uint32) (DirectoryNode, error) {
	d.Lock()
	defer d.Unlock()
	node := d.acquire(id)
	if node == nil {
		return nil, fs.ErrNotFound
	}
	return node, nil
}

// Insert adds a node to the dcache
// Precondition: Key does NOT exist in dcache
//
// Thread-safe
func (d *Dcache) Insert(id uint32, n DirectoryNode) {
	d.Lock()
	d.insert(id, n)
	d.Unlock()
}

// Acquire increments the number of references to a node in the dcache
// Precondition: Key exists in dcache
//
// Thread-safe
func (d *Dcache) Acquire(id uint32) {
	d.Lock()
	if n := d.acquire(id); n == nil {
		panic("Node did not exist in dcache")
	}
	d.Unlock()
}

// Release decreases a reference to a directory in the dcache
// Precondition: Key exists in dcache
//
// Thread-safe
func (d *Dcache) Release(id uint32) {
	d.Lock()
	defer d.Unlock()
	entry, ok := d.entries[id]
	if !ok {
		panic("Releasing node which does not exist in dcache")
	}
	entry.refs--
	if entry.refs < 0 {
		panic("Invalid refcounting")
	} else if entry.refs == 0 {
		delete(d.entries, id)
	}
}

// Transfer moves count references from one ID to another.
func (d *Dcache) Transfer(src, dst uint32, count int) {
	d.Lock()
	defer d.Unlock()
	srcEntry, srcOk := d.entries[src]
	dstEntry, dstOk := d.entries[dst]
	if !srcOk || !dstOk {
		panic("Invalid src/dst; cannot transfer")
	}

	srcEntry.refs -= count
	if srcEntry.refs < 0 {
		panic("Invalid refcounting")
	} else if srcEntry.refs == 0 {
		delete(d.entries, src)
	}
	dstEntry.refs += count
}

func (d *Dcache) acquire(id uint32) DirectoryNode {
	entry, ok := d.entries[id]
	if ok {
		entry.refs++
		return entry.n
	}
	return nil
}

func (d *Dcache) insert(id uint32, n DirectoryNode) {
	if _, ok := d.entries[id]; ok {
		panic("Directory already exists in dcache")
	}
	d.entries[id] = &dcacheEntry{
		n:    n,
		refs: 1,
	}
}
