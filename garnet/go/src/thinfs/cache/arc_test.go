// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cache

import (
	"math/rand"
	"testing"
	"time"
)

const (
	// Arbitrarily chosen.
	defaultSize   = 571
	numIterations = 385494
)

type store struct {
	get func(Key) Value
	put func(Key, Value)
}

func (s store) Get(k Key) Value {
	return s.get(k)
}

func (s store) Put(k Key, v Value) {
	s.put(k, v)
}

func setUp(t *testing.T) *rand.Rand {
	seed := time.Now().UTC().UnixNano()
	t.Log("Seed is ", seed)
	r := rand.New(rand.NewSource(seed))

	return r
}

func TestGet(t *testing.T) {
	r := setUp(t)
	s := store{
		get: func(k Key) Value {
			return Value(k)
		},
	}

	arc := New(defaultSize, s)
	k := r.Int63()
	if v := arc.Get(k).Value.(int64); v != k {
		t.Errorf("arc.Get(%v) = %v; want %v\n", k, v, k)
	}
}

func TestPut(t *testing.T) {
	r := setUp(t)
	key, val := r.Int(), r.Int()
	var called bool

	s := store{
		put: func(k Key, v Value) {
			called = true
			if k != key {
				t.Errorf("put: k = %v; want %v\n", k, key)
			}
			if v != val {
				t.Errorf("put: v = %v; want %v\n", v, val)
			}
		},
	}

	arc := New(defaultSize, s)
	arc.Put(key, val)
	arc.Flush() // Will call s.put

	if !called {
		t.Error("arc.Flush() did not call Put on the BackingStore")
	}
}

func TestModify(t *testing.T) {
	r := setUp(t)
	var called bool

	s := store{
		get: func(k Key) Value {
			return Value(k)
		},
		put: func(k Key, v Value) {
			called = true
			if v.(int64) != ^k.(int64) {
				t.Errorf("put: Value is %v for Key %v; want %v\n", v, k, ^k.(int64))
			}
		},
	}

	arc := New(defaultSize, s)
	k := r.Int63()
	entry := arc.Get(k)
	entry.Value = ^k
	entry.IsDirty = true

	arc.Flush()

	if !called {
		t.Error("arc.Flush() did not call Put on the BackingStore")
	}
}

type testEntry struct {
	isDirty bool
	value   Value
}

type op int

const (
	get op = iota
	put
	modify
)

func getOneElem(m map[Key]*testEntry) Key {
	for k := range m {
		return k
	}

	return nil
}

func TestFuzz(t *testing.T) {
	r := setUp(t)

	checker := make(map[Key]*testEntry)
	s := store{
		get: func(k Key) Value {
			checker[k] = &testEntry{
				isDirty: false,
				value:   ^k.(int),
			}
			return ^k.(int)
		},
		put: func(k Key, v Value) {
			val, ok := checker[k]
			if !ok {
				t.Errorf("Putting key %v in the backing store when it shouldn't exist in the cache\n", k)
			}
			if !val.isDirty {
				t.Errorf("Attempting to commit non-dirty entry for key %v to the backing store\n", k)
			}
			if val.value != v {
				t.Errorf("Putting value v = %v for key %v; want %v\n", v, k, val.value)
			}
			delete(checker, k)
		},
	}

	arc := New(defaultSize, s)

	for i := 0; i < numIterations; i++ {
		operation := op(r.Intn(3))
		var k Key
		if len(checker) > 0 && r.Int()&0x1 == 0 {
			// Use an existing key.
			k = getOneElem(checker)
		} else {
			k = Key(r.Int())
		}

		switch operation {
		case get:
			entry := arc.Get(k)
			if entry.Value != checker[k].value {
				t.Errorf("arc.Get(%v) = %v; want %v\n", k, entry.Value, checker[k].value)
			}
		case put:
			arc.Put(k, Value(k))
			checker[k] = &testEntry{
				isDirty: true,
				value:   Value(k),
			}
		case modify:
			entry := arc.Get(k)
			entry.IsDirty = true
			entry.Value = -k.(int)
			checker[k] = &testEntry{
				isDirty: true,
				value:   -k.(int),
			}
		}
	}

	arc.Flush()
}
