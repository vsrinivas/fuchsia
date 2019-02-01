// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package cache provides an object cache that uses an adaptive replacement policy
// described by Mediddo & Modha in "Outperforming LRU with an Adaptive Replacement
// Cache Algorithm".
package cache

import (
	"container/list"
)

// Key represents a key to an object in the cache.
type Key interface{}

// Value represents a value associated with a key in the cache.
type Value interface{}

// sigh...
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// Entry represents a single entry in the cache.
type Entry struct {
	// The list.Element corresponding to this entry.
	elem *list.Element

	// The list.List to which this entry belongs.
	list *list.List

	// The key for this entry.
	key Key

	// The value associated with the key for this entry.
	Value Value

	// Indicates whether this entry has been modified.  Callers must set this field
	// to true when they modify Value.  The cache will only commit this Entry to the BackingStore
	// if IsDirty is true.
	IsDirty bool
}

// BackingStore defines the interface that the cache expects from the storage medium for which
// it is acting as a cache.
type BackingStore interface {
	// Get is called by the cache when a requested key is not found in the cache and needs
	// to be read in from the BackingStore.
	Get(Key) Value

	// Put is called by the cache when a dirty entry is flushed and should be committed to
	// the BackingStore.
	Put(Key, Value)
}

// C represents an object cache.
type C struct {
	lru     *list.List // Least recently used.
	lruhist *list.List // Least recently used history.
	lfu     *list.List // Least frequently used.
	lfuhist *list.List // Least frequently used history.

	p       int            // The adaptive parameter.
	size    int            // Size of the cache.
	bs      BackingStore   // Backing store for the cache.
	entries map[Key]*Entry // The actual cache entries.
}

// New returns a new, initialized Adaptive Replacement Cache.
func New(size int, bs BackingStore) *C {
	return &C{
		lru:     list.New(),
		lruhist: list.New(),
		lfu:     list.New(),
		lfuhist: list.New(),
		p:       0,
		size:    size,
		entries: make(map[Key]*Entry),
		bs:      bs,
	}
}

// evict moves the last Entry from top to the front of bottom, clearing the value associated with the
// Entry and committing it to the BackingStore, if necessary.
func (c *C) evict(top, bottom *list.List) {
	victim := top.Back().Value.(*Entry)

	// Flush the entry.
	if victim.IsDirty {
		c.bs.Put(victim.key, victim.Value)
		victim.IsDirty = false
	}
	victim.Value = nil

	// Move onto bottom.
	top.Remove(victim.elem)
	victim.elem = bottom.PushFront(victim)
	victim.list = bottom
}

// replace implements the REPLACE subroutine from the paper.
func (c *C) replace(e *Entry) {
	lrulen := c.lru.Len()
	if lrulen >= 1 && ((e.list == c.lfuhist && lrulen == c.p) || lrulen > c.p) {
		c.evict(c.lru, c.lruhist)
	} else {
		c.evict(c.lfu, c.lfuhist)
	}
}

// remove deletes an Entry from the cache, committing its value to the BackingStore, if necessary.
func (c *C) remove(e *Entry) {
	if e.IsDirty {
		c.bs.Put(e.key, e.Value)
		e.IsDirty = false
	}
	e.Value = nil

	e.list.Remove(e.elem)
	e.list = nil
	e.elem = nil
	delete(c.entries, e.key)
}

// handleMiss implements Case IV from the paper.
func (c *C) handleMiss(e *Entry) {
	if l1len := c.lru.Len() + c.lruhist.Len(); c.size == l1len {
		if c.lru.Len() < c.size {
			c.remove(c.lruhist.Back().Value.(*Entry))
			c.replace(e)
		} else {
			c.remove(c.lru.Back().Value.(*Entry))
		}
	} else if l2len := c.lfu.Len() + c.lfuhist.Len(); l1len < c.size && l1len+l2len >= c.size {
		if l1len+l2len == 2*c.size {
			c.remove(c.lfuhist.Back().Value.(*Entry))
		}
		c.replace(e)
	}
	e.elem = c.lru.PushFront(e)
}

// handleHit implements Case I from the paper.
func (c *C) handleHit(e *Entry) {
	e.list.Remove(e.elem)
	e.elem = c.lfu.PushFront(e)
	e.list = c.lfu
}

// handleFakeHit implements Case II and Case III from the paper.
func (c *C) handleFakeHit(e *Entry) {
	// Adapt p.
	if e.list == c.lruhist {
		c.p = min(c.size, c.p+max(c.lfuhist.Len()/c.lruhist.Len(), 1))
	} else {
		c.p = max(0, c.p-max(c.lruhist.Len()/c.lfuhist.Len(), 1))
	}
	c.replace(e)

	c.handleHit(e)
}

// Get returns the Entry containing the Value for the Key k, fetching it from the BackingStore
// if necessary.  Callers _must_ set the IsDirty field for the returned Entry if they change the
// Value in the Entry to ensure that the change is propagated to the BackingStore.  Callers must
// also not retain the returned Entry.
func (c *C) Get(k Key) *Entry {
	e, ok := c.entries[k]
	if !ok {
		e = &Entry{
			list:    c.lru,
			key:     k,
			Value:   c.bs.Get(k),
			IsDirty: false,
		}
		c.entries[k] = e

		c.handleMiss(e)
	} else if e.list == c.lruhist || e.list == c.lfuhist {
		e.Value = c.bs.Get(k)

		c.handleFakeHit(e)
	} else {
		c.handleHit(e)
	}

	return e
}

// Put associates the Value v with the Key k and stores it in the cache.  It additionally marks the
// Entry dirty so that the new Value will be propagated to the BackingStore.  Callers may wish to
// use Put when they want to completely replace the Value associated with some Key and want to avoid
// a potentially expensive lookup for the Key in the BackingStore.
func (c *C) Put(k Key, v Value) {
	e, ok := c.entries[k]
	if !ok {
		e = &Entry{
			list:    c.lru,
			key:     k,
			Value:   v,
			IsDirty: true,
		}
		c.entries[k] = e

		c.handleMiss(e)
	} else if e.list == c.lruhist || e.list == c.lfuhist {
		e.Value = v
		e.IsDirty = true

		c.handleFakeHit(e)
	} else {
		e.Value = v
		e.IsDirty = true

		c.handleHit(e)
	}
}

// Flush commits all dirty Entries in the cache to the BackingStore.
func (c *C) Flush() {
	for _, e := range c.entries {
		if !e.IsDirty {
			continue
		}

		c.bs.Put(e.key, e.Value)
		e.IsDirty = false
	}
}
