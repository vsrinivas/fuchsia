// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package cache

import "container/list"

// A Key may be any value that is comparable.
type Key interface{}

// Cache is the interface for cache.
type Cache interface {
	// Adds a value to the cache.
	Add(key Key, value interface{}) bool

	// Returns key's value from the cache.
	Get(key Key) (value interface{}, ok bool)

	// Check if a key exsists in cache.
	Contains(key Key) (ok bool)

	// Removes a key from the cache.
	Remove(key Key) bool

	// Returns the number of items in the cache.
	Len() int

	// Clear all cache entries.
	Clear()
}

// LRUCache is a simple LRU cache.
type LRUCache struct {
	// Size is the maximum number of entries before an item is evicted.
	// Zero means no limit on the number of entries.
	Size uint

	ll    *list.List
	cache map[interface{}]*list.Element
}

type entry struct {
	key   Key
	value interface{}
}

// Adds a value to the cache and updates the "recently used"-ness of the key.
func (c *LRUCache) Add(key Key, value interface{}) {
	if c.cache == nil {
		c.cache = make(map[interface{}]*list.Element)
		c.ll = list.New()
	}
	if e, ok := c.cache[key]; ok {
		c.ll.MoveToFront(e)
		e.Value.(*entry).value = value
		return
	}
	e := c.ll.PushFront(&entry{key, value})
	c.cache[key] = e
	if c.Size != 0 && uint(c.ll.Len()) > c.Size {
		v := c.ll.Remove(c.ll.Back())
		delete(c.cache, v.(*entry).key)
	}
}

// Returns key's value from the cache and updates the "recently used"-ness.
func (c *LRUCache) Get(key Key) (interface{}, bool) {
	if c.cache == nil {
		return nil, false
	}
	if e, ok := c.cache[key]; ok {
		c.ll.MoveToFront(e)
		return e.Value.(*entry).value, true
	}
	return nil, false
}

// Contains checks if a key exsists in cache without updating the recent-ness.
func (c *LRUCache) Contains(key Key) bool {
	if c.cache == nil {
		return false
	}
	_, ok := c.cache[key]
	return ok
}

// Removes a key from the cache.
func (c *LRUCache) Remove(key Key) interface{} {
	if c.cache == nil {
		return nil
	}
	if e, ok := c.cache[key]; ok {
		c.ll.Remove(e)
		delete(c.cache, key)
		return e.Value.(*entry).value
	}
	return nil
}

// Returns the number of items in the cache.
func (c *LRUCache) Len() int {
	if c.cache == nil {
		return 0
	}
	return c.ll.Len()
}

// Clear all cache entries.
func (c *LRUCache) Clear() {
	c.ll = nil
	c.cache = nil
}
