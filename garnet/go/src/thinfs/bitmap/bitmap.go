// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package bitmap provides bitmaps which can be used as trivial inode / block map allocation tables
package bitmap

import (
	"errors"
	"sync"
)

// Bitmap describes a byte slice which acts as a slice of bits.
type Bitmap struct {
	lo uint32 // Lowest value which should be allocated
	hi uint32 // Highest value which should be allocated

	sync.Mutex
	slice         []byte
	lastAllocated uint32
}

// New transforms byteSlice into a bitmap.
// When allocating from the bitmap, the structure will search in the range of lo (inclusive) to
// hi (exclusive).
func New(b []byte, lo, hi uint32) *Bitmap {
	if hi > uint32(len(b)*8) || hi <= lo {
		panic("Invalid hi argument")
	}
	return &Bitmap{
		slice: b,
		lo:    lo,
		hi:    hi,
	}
}

// Copy returns a copy of the bitmap. lo and hi are ignored; the bitmap is returned exactly as a
// byte slice.
func (bm *Bitmap) Copy() []byte {
	c := make([]byte, len(bm.slice))
	bm.Lock()
	for i := range c {
		c[i] = bm.slice[i]
	}
	bm.Unlock()
	return c
}

// Allocate finds bits set to 0 in the range of [lo, hi), and sets them to '1'. It returns an error
// if it cannot allocate count bits.
func (bm *Bitmap) Allocate(count uint32) ([]uint32, error) {
	bits := make([]uint32, count)
	bm.Lock()
	defer bm.Unlock()
	start := bm.lastAllocated + 1
	if start < bm.lo || bm.hi < start {
		start = bm.lo
	}
	for i := start; i < bm.hi; i++ {
		if !bm.get(i) {
			bm.set(i, true)
			bits[uint32(len(bits))-count] = i
			count--
			bm.lastAllocated = i
			if count == 0 {
				return bits, nil
			}
		}
	}

	for i := bm.lo; i < start; i++ {
		if !bm.get(i) {
			bm.set(i, true)
			bits[uint32(len(bits))-count] = i
			count--
			bm.lastAllocated = i
			if count == 0 {
				return bits, nil
			}
		}
	}

	// We don't have enough space to allocate count bits. Free them.
	for i := range bits {
		bm.set(bits[i], false)
	}
	return nil, errors.New("Not enough free spots found")
}

// Free flips the bits in the 'toFree' slice to zero, 'freeing' them.
func (bm *Bitmap) Free(toFree []uint32) {
	if len(toFree) == 0 {
		return
	}
	bm.Lock()
	for i := range toFree {
		if toFree[i] != 0 {
			bm.set(toFree[i], false)
		}
	}
	bm.Unlock()
}

// Get returns the status of the bit in position i.
func (bm *Bitmap) Get(i uint32) bool {
	bm.Lock()
	defer bm.Unlock()
	return bm.get(i)
}

// Unlocked internal mechanism to get a bit status.
func (bm *Bitmap) get(i uint32) bool {
	if i >= uint32(len(bm.slice)*8) {
		panic("Bitmap Get: Index out of range")
	}

	byteIndex := i / 8
	bitIndex := i % 8

	targetByte := bm.slice[byteIndex]
	return targetByte&(1<<bitIndex) != 0
}

// Unlocked internal mechanism to set a bit status.
func (bm *Bitmap) set(i uint32, v bool) {
	if i >= uint32(len(bm.slice)*8) {
		panic("Bitmap Set: Index out of range")
	}

	byteIndex := i / 8
	bitIndex := i % 8

	if v {
		bm.slice[byteIndex] |= (1 << bitIndex)
	} else {
		bm.slice[byteIndex] &^= (1 << bitIndex)
	}
}
