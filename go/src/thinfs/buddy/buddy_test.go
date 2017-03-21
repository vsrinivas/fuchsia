// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buddy

import (
	"math/rand"
	"reflect"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/bitops"
)

const (
	start    uint64 = 0
	size            = 4 * 1024 * 1024 * 1024 // 4G
	minAlloc        = 4 * 1024               // 4K
	maxAlloc        = 4 * 1024 * 1024        // 4M
)

const numIterations = 5000

func randomElem(m map[uint64]uint) uint64 {
	for k := range m {
		return k
	}

	return 0
}

func setUp(t *testing.T) (*Allocator, map[uint64]uint) {
	a, err := NewAllocator(start, size, minAlloc, maxAlloc)
	if err != nil {
		t.Error("Error creating binary buddy alocator: ", err)
		t.FailNow()
	}
	seed := time.Now().UTC().UnixNano()
	t.Log("seed is ", seed)
	r := rand.New(rand.NewSource(seed))

	alloc := make(map[uint64]uint)
	for i := 0; i < numIterations; i++ {
		if r.Int()&0x1 == 0 {
			// Free a chunk
			addr := randomElem(alloc)
			delete(alloc, addr)
			a.Free(addr)
			continue
		}

		// Allocate a chunk
		size := uint64(r.Intn(int(maxAlloc)))
		addr, err := a.Alloc(size)
		if err != nil {
			t.Errorf("Error allocating chunk of size %v: %v\n", size, err)
			t.FailNow()
		}
		if _, ok := alloc[addr]; ok {
			t.Errorf("Address %v has already been allocated\n", addr)
			t.FailNow()
		}

		var order uint
		if isPowerOfTwo(size) {
			order = bitops.FFS(size)
		} else {
			order = 64 - bitops.CLZ(size)
		}
		alloc[addr] = order
	}

	return a, alloc
}

func TestPolicy(t *testing.T) {
	_, alloc := setUp(t)

	for addr, expected := range alloc {
		// 0 is kind of a special address since it's always valid.
		if addr == 0 {
			continue
		}
		if actual := bitops.FFS(addr); actual < expected {
			t.Errorf("Address %#x has order %v; want order %v or larger\n", addr, actual, expected)
		}
	}
}

func TestMarshalUnmarshal(t *testing.T) {
	a, _ := setUp(t)

	buf, err := a.MarshalBinary()
	if err != nil {
		t.Error("error marshaling binary: ", err)
		t.FailNow()
	}

	a2 := new(Allocator)
	if err := a2.UnmarshalBinary(buf); err != nil {
		t.Error("error unmarshaling binary: ", err)
		t.FailNow()
	}

	if !reflect.DeepEqual(a, a2) {
		t.Error("a and a2 differ")
	}
}
