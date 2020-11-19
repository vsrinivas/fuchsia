// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

//go:generate go run blockgen.go

package inspect

import (
	"bytes"
	"errors"
	"fmt"
	"sync"
	"syscall/zx"
	"unsafe"
)

const (
	minOrderShift = uint64(4)
	minOrderSize  = 1 << minOrderShift

	maxOrderShift = minOrderShift + uint64(nOrders) - 1
	maxOrderSize  = 1 << maxOrderShift
	minVMOSize    = 4096

	minAllocSize = uint64(unsafe.Sizeof(Block{}))
	maxPayload   = maxOrderSize - uint64(unsafe.Sizeof(Block{}))
)

// BlockIndex is a type that represents the index of a block.
type BlockIndex uint64

// heap holds internal state about the allocator.
type heap struct {
	mtx sync.Mutex

	vmo     zx.VMO
	vmoAddr zx.Vaddr

	curSize         uint64
	maxSize         uint64
	allocatedBlocks uint64

	freeBlocks [8]BlockIndex
}

func getBuddy(i BlockIndex, o BlockOrder) BlockIndex {
	return i ^ (1 << o)
}

// newHeap returns a pointer to a new heap object that can be used for allocating Blocks. When an
// error is encountered, the supplied pointer will be nil, and the error will signify which operation
// failed.
func newHeap(maxSize uint64) (*heap, error) {
	// Ensure that we're at least the minimum size, and pad the max size to a page boundary.
	if maxSize < minVMOSize {
		maxSize = minVMOSize
	}

	if maxSize&0xfff != 0 {
		maxSize += (4096 - (maxSize & 4095)) & 4095
	}

	// Create and map a new VMO of the requested size.
	vmo, err := zx.NewVMO(maxSize, zx.VMOOption(0))
	if err != nil {
		return nil, err
	}

	vaddr, err := zx.VMARRoot.Map(0, vmo, 0, maxSize, zx.VMFlagPermRead|zx.VMFlagPermWrite)
	if err != nil {
		return nil, err
	}

	// Initialize the heap and make minVMOSize of it available for allocation.
	h := &heap{
		curSize: 0,
		maxSize: maxSize,
		vmo:     vmo,
		vmoAddr: vaddr,
	}

	h.extend(minVMOSize)

	return h, nil
}

func (h *heap) dump() (string, error) {
	out := bytes.NewBuffer([]byte{})
	fmt.Fprintf(out, "heap state: %+v\n", h)
	off := uint64(0)
	for off < h.curSize {
		if h.curSize-off < uint64(unsafe.Sizeof(Block{})) {
			return out.String(), fmt.Errorf("Block at %d doesn't fit in remaining space\n", off)
		}

		b := (*Block)(unsafe.Pointer(uintptr(h.vmoAddr) + uintptr(off)))
		order := b.GetOrder()
		if order > BlockOrder(maxOrderShift) {
			return out.String(), fmt.Errorf("block order %d at offset %d invalid\n", order, off)
		}

		if BlockOrder(h.curSize-off) < orderSize[order] {
			return out.String(), fmt.Errorf("block order %d can't fit in remaining space at offset %d\n", order, off)
		}

		fmt.Fprintf(out, "Block[%d]: %s\n", off, b)
		off += uint64(orderSize[order])
	}

	return out.String(), nil
}

// close releases resources associated with the heap when this call is the final reference to the heap.
// It returns an error if releasing resources fails for any reason.
func (h *heap) close() error {
	if h.allocatedBlocks > 0 {
		return fmt.Errorf("won't free heap with %d outstanding allocations", h.allocatedBlocks)
	}

	h.mtx.Lock()
	defer h.mtx.Unlock()

	if err := zx.VMARRoot.Unmap(h.vmoAddr, h.maxSize); err != nil {
		return err
	}

	return h.vmo.Close()
}

// getVMO returns a read-only handle to the heap's VMO. On error, it returns an invalid handle
// and an error signifying the failure. The returned duplicated handle must be closed using the
// heap's CloseVMO method.
func (h *heap) getVMO() (zx.VMO, error) {
	h.mtx.Lock()
	defer h.mtx.Unlock()

	handle := zx.Handle(h.vmo)
	d, err := handle.Duplicate(zx.RightRead)
	return zx.VMO(d), err
}

// getBlock returns a pointer to a Block at the specified index. It returns a nil Block and an error when the supplied
// index is out of bounds of the underlying VMO, or if the span covered by the block would be out of bounds.
func (h *heap) getBlock(i BlockIndex) (*Block, error) {
	// Check that the smallest block could fit within the VMO.
	if uintptr(h.vmoAddr)+uintptr(i*minOrderSize)+unsafe.Sizeof(Block{}) > uintptr(h.vmoAddr)+uintptr(h.maxSize) {
		return nil, fmt.Errorf("block %d is out of bounds", i)
	}

	// Check that the block at this index doesn't extend past the end of the VMO.
	b := (*Block)(unsafe.Pointer(uintptr(h.vmoAddr) + uintptr(i*minOrderSize)))
	if uintptr(h.vmoAddr)+uintptr(i*minOrderSize)+uintptr(orderSize[b.GetOrder()]) > uintptr(h.vmoAddr)+uintptr(h.maxSize) {
		return nil, fmt.Errorf("block %d extends out of bounds", i)
	}

	return b, nil

}

func (h *heap) isFree(i BlockIndex, expectedOrder BlockOrder) bool {
	if i >= BlockIndex(h.curSize/minOrderSize) {
		return false
	}

	b, err := h.getBlock(i)
	if err != nil {
		return false
	}

	return b.GetType() == FreeBlockType && b.GetOrder() == expectedOrder
}

// allocate returns the index of a block that fits the requested size. On error, it returns a zeroed
// BlockIndex and a non-nil error.
func (h *heap) allocate(minSize uint64) (BlockIndex, error) {
	h.mtx.Lock()
	defer h.mtx.Unlock()

	// Determine the minimum size class that will fit the requested allocation.
	minOrder := nOrders
	for i, v := range orderSize {
		if BlockOrder(minSize) <= v {
			minOrder = uint(i)
			break
		}
	}
	if minOrder >= 8 {
		return BlockIndex(0), errors.New("supplied size doesn't fit in any pool buckets")
	}

	// Try to find a free block in this size class.
	nextOrder := nOrders
	for i := minOrder; i < nOrders; i++ {
		if h.isFree(h.freeBlocks[i], BlockOrder(i)) {
			nextOrder = i
			break
		}
	}

	// We failed to find a free block, so extend the range of the VMO and use a newly-free block.
	if nextOrder == nOrders {
		if err := h.extend(h.curSize * 2); err != nil {
			return BlockIndex(0), err
		}
		nextOrder = nOrders - 1
	}

	// We have a block; split it repeatedly until it is the minimum acceptable size to fit the allocation.
	bi := h.freeBlocks[nextOrder]
	for {
		b, err := h.getBlock(bi)
		if err != nil {
			return 0, err
		}
		if b.GetOrder() > BlockOrder(minOrder) {
			if h.splitBlock(bi) == false {
				return 0, errors.New("failed to split block")
			}
		} else {
			b.SetType(ReservedBlockType)
			break
		}
	}

	h.removeFree(bi)
	h.allocatedBlocks++

	return bi, nil
}

// free frees the block at the supplied BlockIndex.
func (h *heap) free(i BlockIndex) {
	h.mtx.Lock()
	defer h.mtx.Unlock()

	block, err := h.getBlock(i)
	if err != nil {
		return
	}

	buddyIdx := getBuddy(i, block.GetOrder())
	buddy, err := h.getBlock(buddyIdx)
	if err != nil {
		return
	}

	// Merge buddies of the freed block until the buddy is either not free, or we hit the maximum block size.
	for buddy.GetType() == FreeBlockType && block.GetOrder() < BlockOrder(nOrders-1) && block.GetOrder() == buddy.GetOrder() {
		h.removeFree(buddyIdx)
		blockPtr := uintptr(unsafe.Pointer(block))
		buddyPtr := uintptr(unsafe.Pointer(buddy))
		if buddyPtr < blockPtr {
			block, buddy = buddy, block
			i, buddyIdx = buddyIdx, i
		}

		block.SetOrder(block.GetOrder() + 1)
		buddyIdx = getBuddy(i, block.GetOrder())
		buddy, err = h.getBlock(buddyIdx)
		if err != nil {
			return
		}
	}

	// Put the block back onto the freelist.
	block.Free(block.GetOrder(), h.freeBlocks[block.GetOrder()])
	h.freeBlocks[block.GetOrder()] = i
	h.allocatedBlocks--
}

func (h *heap) splitBlock(i BlockIndex) bool {
	h.removeFree(i)
	b, err := h.getBlock(i)
	if err != nil {
		return false
	}

	if b.GetOrder() >= BlockOrder(nOrders) {
		return false
	}

	// Lower the order on the original block and find its new buddy, adding both to the freelist of the new order.
	newOrder := b.GetOrder() - 1
	buddyIdx := getBuddy(i, newOrder)
	buddy, err := h.getBlock(buddyIdx)
	if err != nil {
		return false
	}

	b.Free(newOrder, buddyIdx)
	buddy.Free(newOrder, h.freeBlocks[newOrder])

	h.freeBlocks[newOrder] = i

	return true
}

func (h *heap) removeFree(i BlockIndex) bool {
	b, err := h.getBlock(i)
	if err != nil {
		return false
	}
	rmBlock := b.AsFreeBlock()

	order := rmBlock.GetOrder()

	if order >= BlockOrder(nOrders) {
		return false
	}

	// Fast path: unlink this block from the head of the list.
	next := h.freeBlocks[order]
	if next == i {
		h.freeBlocks[order] = BlockIndex(rmBlock.GetNextFree())
		return true
	}

	// Slow path: iterate through the freelist until we find the position for this block and unlink it from that position.
	for h.isFree(next, order) {
		b, err = h.getBlock(next)
		if err != nil {
			return false
		}
		cur := b.AsFreeBlock()

		next = BlockIndex(cur.GetNextFree())
		if next == i {
			cur.SetNextFree(rmBlock.GetNextFree())
			return true
		}
	}

	return false
}

func (h *heap) extend(newSize uint64) error {
	if h.curSize == h.maxSize && newSize > h.maxSize {
		return errors.New("heap already at maximum size")
	}

	// Bound the new size to the maximum size
	if newSize > h.maxSize {
		newSize = h.maxSize
	}

	if h.curSize > newSize {
		return nil
	}

	minIdx := BlockIndex(h.curSize / minOrderSize)
	lastIdx := h.freeBlocks[nOrders-1]
	curIdx := BlockIndex((newSize - (newSize & (minVMOSize - 1))) / minOrderSize)
	for {
		curIdx -= maxOrderSize / minOrderSize
		b, err := h.getBlock(curIdx)
		if err != nil {
			return err
		}

		b.Free(BlockOrder(nOrders-1), lastIdx)
		lastIdx = curIdx

		if curIdx <= minIdx {
			break
		}
	}

	h.freeBlocks[nOrders-1] = lastIdx
	h.curSize = newSize

	return nil
}
