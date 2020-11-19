// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package inspect

import (
	"testing"
	"unsafe"
)

type debugBlock struct {
	i BlockIndex
	t BlockType
	o BlockOrder
	e error
}

func matchLayout(t *testing.T, h *heap, l []debugBlock) {
	var db debugBlock

	i := 0
	off := uint64(0)
	for off < h.curSize {
		if h.curSize-off < uint64(unsafe.Sizeof(Block{})) {
			t.Fatalf("block doesn't fit in remaining space")
		}

		if i >= len(l) {
			t.Fatalf("offset doesn't match blocks")
		}

		b := (*Block)(unsafe.Pointer(uintptr(h.vmoAddr) + uintptr(off)))
		order := b.GetOrder()
		if order > BlockOrder(maxOrderShift) {
			t.Fatalf("invalid block order found")
		}

		if BlockOrder(h.curSize-off) < orderSize[order] {
			t.Fatalf("block can't fit in remaining space")
		}

		db = debugBlock{BlockIndex(off / minOrderSize), b.GetType(), b.GetOrder(), nil}
		tb := l[i]
		if db.i != tb.i {
			t.Errorf("heap block %d has index %d; want index %d", i, db.i, tb.i)
		}
		if db.t != tb.t {
			t.Errorf("heap block %d has type %d; want type %d", i, db.t, tb.t)
		}
		if db.o != tb.o {
			t.Errorf("heap block %d has order %d; want order %d", i, db.o, tb.o)
		}

		off += uint64(orderSize[order])
		i++
	}

}

func TestHeapCreation(t *testing.T) {
	h, err := newHeap(4096)
	if err != nil {
		t.Fatalf("failed to create heap")
	}

	blockLayout := []debugBlock{
		{0, FreeBlockType, 7, nil},
		{128, FreeBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)
}

func TestHeapAllocation(t *testing.T) {
	h, err := newHeap(4096)
	if err != nil {
		t.Fatalf("couldn't create heap: %v", err)
	}
	defer func() {
		if err := h.close(); err != nil {
			t.Errorf("failed to close heap: %v", err)
		}
	}()

	// Allocating a series of small blocks should result in in-order block indexes.
	expectedBlocks := []BlockIndex{0, 1, 2, 3, 4, 5}
	for i, v := range expectedBlocks {
		bi, err := h.allocate(minAllocSize)
		if err != nil {
			t.Errorf("failed allocation %d: %v", i, err)
		}

		if bi != v {
			t.Errorf("expectedBlocks: h.allocate(%d) = %d; want %d", minAllocSize, bi, i)
		}
	}

	// Free blocks, leaving some in the middle to ensure they chain.
	freeBlocks := []BlockIndex{2, 4, 0}
	for _, v := range freeBlocks {
		h.free(v)
	}

	// Allocate the small blocks again to see that we get the same ones in reverse order.
	revBlocks := []BlockIndex{0, 4, 2}
	for i, v := range revBlocks {
		bi, err := h.allocate(minAllocSize)
		if err != nil {
			t.Errorf("failed reverse allocation %d: %v\n", i, err)
		}

		if bi != v {
			t.Errorf("revBlocks: h.allocate(%d) = %d; want %d", minAllocSize, bi, v)
		}
	}

	// Free all but the first two blocks and check that the heap structure matches.
	freeBlocks = []BlockIndex{4, 2, 3, 5}
	for _, v := range freeBlocks {
		h.free(v)
	}

	blockLayout := []debugBlock{
		{0, ReservedBlockType, 0, nil},
		{1, ReservedBlockType, 0, nil},
		{2, FreeBlockType, 1, nil},
		{4, FreeBlockType, 2, nil},
		{8, FreeBlockType, 3, nil},
		{16, FreeBlockType, 4, nil},
		{32, FreeBlockType, 5, nil},
		{64, FreeBlockType, 6, nil},
		{128, FreeBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)

	// Make a small free hole at offset 0, then allocate something large to ensure it occupies the largest block.
	h.free(0)
	bi, err := h.allocate(2048)
	if err != nil {
		t.Errorf("allocation for large allocation test failed: %v", err)
	}
	if bi != BlockIndex(128) {
		t.Errorf("large allocation got block %d; want 128", bi)
	}

	// Free the last small allocation; this should merge all buddies allowing us to grab a large allocation in the first
	// half of the VMO.
	h.free(1)
	bi, err = h.allocate(2048)
	if err != nil {
		t.Errorf("allocation for second large allocation test failed: %v", err)
	}
	if bi != BlockIndex(0) {
		t.Errorf("second large allocation got block %d; want 0", bi)
	}

	blockLayout = []debugBlock{
		{0, ReservedBlockType, 7, nil},
		{128, ReservedBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)

	h.free(0)
	h.free(128)
}

func TestReverseFree(t *testing.T) {
	h, err := newHeap(4096)
	if err != nil {
		t.Fatalf("couldn't create heap: %v", err)
	}
	defer func() {
		if err := h.close(); err != nil {
			t.Errorf("failed to close heap: %v", err)
		}
	}()

	bi, err := h.allocate(1024)
	if err != nil {
		t.Errorf("failed to perform first allocation: %v", err)
	}
	if bi != BlockIndex(0) {
		t.Errorf("first allocation got index %d; want 0", bi)
	}

	bi, err = h.allocate(1024)
	if err != nil {
		t.Errorf("failed to perform second allocation: %v", err)
	}
	if bi != BlockIndex(64) {
		t.Errorf("second allocation got index %d; want 64", bi)
	}

	h.free(0)
	h.free(64)

	// Ensure freed blocks are merged and we can use the whole space at index 0.
	bi, err = h.allocate(2048)
	if err != nil {
		t.Errorf("final allocation failed: %v", err)
	}
	if bi != BlockIndex(0) {
		t.Errorf("final allocation got index %d; want 0", bi)
	}

	h.free(0)

	blockLayout := []debugBlock{
		{0, FreeBlockType, 7, nil},
		{128, FreeBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)
}

func TestMerge(t *testing.T) {
	h, err := newHeap(4096)
	if err != nil {
		t.Fatalf("couldn't create heap: %v", err)
	}
	defer func() {
		if err := h.close(); err != nil {
			t.Errorf("failed to close heap: %v", err)
		}
	}()

	// In this test, we perform 4 small allocations, and then free the allocations at indices 2, 0, and 1.
	// This results in the final free seeing a situation like:
	// FREE | FREE | FREE | RESERVED
	// The first two of these spaces should be merged into a block of order 1; the allocation at the end
	// should prevent merging into a block of order 2.
	expectedBlocks := []BlockIndex{0, 1, 2, 3}
	for i, v := range expectedBlocks {
		bi, err := h.allocate(minAllocSize)
		if err != nil {
			t.Errorf("failed allocation %d: %v", i, err)
		}

		if bi != v {
			t.Errorf("expectedBlocks: h.allocate(%d) = %d; want %d", minAllocSize, bi, v)
		}
	}

	h.free(2)
	h.free(0)
	h.free(1)

	blockLayout := []debugBlock{
		{0, FreeBlockType, 1, nil},
		{2, FreeBlockType, 0, nil},
		{3, ReservedBlockType, 0, nil},
		{4, FreeBlockType, 2, nil},
		{8, FreeBlockType, 3, nil},
		{16, FreeBlockType, 4, nil},
		{32, FreeBlockType, 5, nil},
		{64, FreeBlockType, 6, nil},
		{128, FreeBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)

	h.free(3)
	matchLayout(t, h, []debugBlock{{0, FreeBlockType, 7, nil}, {128, FreeBlockType, 7, nil}})
}

func TestHeapExtend(t *testing.T) {
	h, err := newHeap(128 * 1024)
	if err != nil {
		t.Fatalf("couldn't create heap: %v", err)
	}
	defer func() {
		if err := h.close(); err != nil {
			t.Errorf("failed to close heap: %v", err)
		}
	}()

	// Allocate several large blocks so the heap needs to be extended.
	expectedBlocks := []BlockIndex{0, 128, 256}
	for i, v := range expectedBlocks {
		bi, err := h.allocate(2048)
		if err != nil {
			t.Errorf("failed allocation %d: %v", i, err)
		}

		if bi != v {
			t.Errorf("expectedBlocks: h.allocate(%d) = %d; want %d", minAllocSize, bi, v)
		}
	}

	blockLayout := []debugBlock{
		{0, ReservedBlockType, 7, nil},
		{128, ReservedBlockType, 7, nil},
		{256, ReservedBlockType, 7, nil},
		{384, FreeBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)

	// Allocate a couple more.
	expectedBlocks = []BlockIndex{384, 512}
	for i, v := range expectedBlocks {
		bi, err := h.allocate(2048)
		if err != nil {
			t.Errorf("failed second allocation %d", i)
		}

		if bi != v {
			t.Errorf("expectedBlocks: h.allocate(%d) = %d; want %d", minAllocSize, bi, v)
		}
	}

	h.free(0)
	h.free(128)
	h.free(256)
	h.free(384)
	h.free(512)
	blockLayout = []debugBlock{
		{0, FreeBlockType, 7, nil},
		{128, FreeBlockType, 7, nil},
		{256, FreeBlockType, 7, nil},
		{384, FreeBlockType, 7, nil},
		{512, FreeBlockType, 7, nil},
		{640, FreeBlockType, 7, nil},
		{768, FreeBlockType, 7, nil},
		{896, FreeBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)
}

func TestHeapExtendFailure(t *testing.T) {
	h, err := newHeap(3 * 4096)
	if err != nil {
		t.Fatalf("couldn't create heap: %v", err)
	}
	defer func() {
		if err := h.close(); err != nil {
			t.Errorf("failed to close heap: %v", err)
		}
	}()

	expectedBlocks := []BlockIndex{0, 128, 256, 384, 512, 640}
	for i, v := range expectedBlocks {
		bi, err := h.allocate(2048)
		if err != nil {
			t.Errorf("failed allocation %d: %v", i, err)
		}

		if bi != v {
			t.Errorf("expectedBlocks: h.allocate(%d) = %d; want %d", minAllocSize, bi, v)
		}
	}

	// Now, a new allocation should fail.
	bi, err := h.allocate(2048)
	if err == nil {
		t.Errorf("expected allocation to fail; it succeeded instead, returning index %d", bi)
	}

	blockLayout := []debugBlock{
		{0, ReservedBlockType, 7, nil},
		{128, ReservedBlockType, 7, nil},
		{256, ReservedBlockType, 7, nil},
		{384, ReservedBlockType, 7, nil},
		{512, ReservedBlockType, 7, nil},
		{640, ReservedBlockType, 7, nil},
	}
	matchLayout(t, h, blockLayout)

	h.free(0)
	h.free(128)
	h.free(256)
	h.free(384)
	h.free(512)
	h.free(640)
}
