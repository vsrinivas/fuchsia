// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package buddy provides bitopsities for allocating and managing space on a block device using
// a binary buddy management scheme.
package buddy

import (
	"encoding/binary"
	"errors"

	"fuchsia.googlesource.com/thinfs/lib/bitops"
	"github.com/golang/glog"
)

var (
	// ErrCorrupted indicates that the serialized representation of a Allocator is corrupted
	// and cannot be used to construct a valid Allocator.
	ErrCorrupted = errors.New("serialized data is corrupted")

	// ErrInvalid indicates that an invalid argument was passed in to a function.
	ErrInvalid = errors.New("invalid argument")

	// ErrNoMem indicates that there is not enough contiguous memory available to satisfy the
	// allocation request.
	ErrNoMem = errors.New("not enough contiguous memory")
)

// freemap represents a set of addresses that are free at a given level in the buddy allocation
// scheme.
type freemap map[uint64]bool

// Allocator represents a binary buddy allocator for managing addresses in a given range.
type Allocator struct {
	freemaps []freemap       // Free addresses at every allocation level.
	spacemap map[uint64]uint // Allocated addresses mapped to the level on which they were allocated.

	orderMin uint // log2(minimum allocation size)
	orderMax uint // log2(maximim allocation size)

	totalFree uint64 // The total free space in the address range managed by the Allocator.
	size      uint64 // The size of the address range managed by the Allocator.

	encodedSize int // The size in bytes of the serialized representation of the Allocator.
}

func getBuddy(address uint64, order uint) uint64 {
	return address ^ (1 << order)
}

func isPowerOfTwo(n uint64) bool {
	return n&(n-1) == 0
}

// getOneElem returns one key from m.  It makes no guarantees about how the key is chosen or that
// multiple calls to getOneElem with the same map will return the same key.  m must be non-empty.
func getOneElem(m freemap) uint64 {
	for k := range m {
		return k
	}

	return 0
}

func (a *Allocator) calculateEncodedSize() {
	a.encodedSize = headerSize
	for i := a.orderMin; i <= a.orderMax; i++ {
		a.encodedSize += int(a.size >> i)
	}
}

func (a *Allocator) numFreeMaps() uint {
	return 1 + a.orderMax - a.orderMin
}

// NewAllocator creates and returns a new instance of a Allocator.  |start| and |size|
// represent the range of addresses that will be managed by the Allocator while |minAlloc|
// and |maxAlloc| represent the sizes of the minimun and maximum chunks that the Allocator
// will allocate, respectively.  |size|, |minAlloc|, and |maxAlloc| must be powers of
// 2 and it's recommended for best performance that |maxAlloc| is no more than 10 orders
// of magnitude larger than |minAlloc|.  Additionally |size| must be a multiple of |minAlloc|.
// NewAllocator returns ErrInvalid if the arguments don't meet the stated requirements.
func NewAllocator(start, size, minAlloc, maxAlloc uint64) (*Allocator, error) {
	if !isPowerOfTwo(start) || !isPowerOfTwo(minAlloc) || !isPowerOfTwo(maxAlloc) {
		if glog.V(1) {
			glog.Error("NewAllocator: start, minAlloc, or maxAlloc is not a power of two")
		}
		return nil, ErrInvalid
	}

	if size%minAlloc != 0 {
		if glog.V(1) {
			glog.Error("NewAllocator: size is not a multiple of minAlloc")
		}
		return nil, ErrInvalid
	}

	if maxAlloc >= size {
		if glog.V(1) {
			glog.Error("NewAllocator: maxAlloc >= size")
		}
		return nil, ErrInvalid
	}

	if maxAlloc < minAlloc {
		if glog.V(1) {
			glog.Error("NewAllocator: maxAlloc < minAlloc")
		}
		return nil, ErrInvalid
	}

	a := &Allocator{
		orderMin:  bitops.FFS(minAlloc),
		orderMax:  bitops.FFS(maxAlloc),
		size:      size,
		totalFree: size,
		spacemap:  make(map[uint64]uint),
	}
	a.calculateEncodedSize()

	a.freemaps = make([]freemap, a.numFreeMaps())
	for i := range a.freemaps {
		a.freemaps[i] = make(freemap)
	}

	for pos := start; pos < size; pos += minAlloc {
		a.free(pos, a.orderMin)
	}
	return a, nil
}

func (a *Allocator) alloc(order uint) (uint64, error) {
	o := int(order - a.orderMin)

	if len(a.freemaps[o]) > 0 {
		chunk := getOneElem(a.freemaps[o])
		delete(a.freemaps[o], chunk)
		return chunk, nil
	}

	if order == a.orderMax {
		return 0, ErrNoMem
	}

	chunk, err := a.alloc(order + 1)
	if err != nil {
		return 0, err
	}
	a.freemaps[o][getBuddy(chunk, order)] = true

	return chunk, nil
}

// Alloc allocates and returns an address to a contiguous memory location that is at least
// |n| bytes in size.  Returns ErrInvalid if |n| is larger than the maximum chunk that the
// Allocator is configured to allocate and returns ErrNoMem if there is no contiguous memory
// available to satisfy the requested allocation size.  ErrNoMem does not necessarily indicate
// that the total amount of free space is less than |n| but rather only that there is no contiguous
// free space >= |n|.
func (a *Allocator) Alloc(n uint64) (uint64, error) {
	var order uint
	if isPowerOfTwo(n) {
		order = bitops.FFS(n)
	} else {
		order = 64 - bitops.CLZ(n)
	}
	if order > a.orderMax {
		if glog.V(1) {
			glog.Errorf("Alloc: requested allocation %v is larger than max allocation %v\n", n, (1 << a.orderMax))
		}
		return 0, ErrInvalid
	}

	if order < a.orderMin {
		order = a.orderMin
	}

	if glog.V(2) {
		glog.Infof("Alloc: allocating %v bytes for requested size %v\n", (1 << order), n)
	}

	addr, err := a.alloc(order)
	if err != nil {
		if glog.V(1) {
			glog.Error("Alloc: ", err)
		}
		return 0, err
	}
	a.spacemap[addr] = order
	a.totalFree -= (1 << order)

	return addr, nil
}

func (a *Allocator) free(addr uint64, order uint) {
	o := int(order - a.orderMin)
	buddy := getBuddy(addr, order)

	if order == a.orderMax || a.freemaps[o][buddy] == false {
		a.freemaps[o][addr] = true
		return
	}
	delete(a.freemaps[o], buddy)

	if buddy < addr {
		addr, buddy = buddy, addr
	}

	a.free(addr, order+1)
}

// Free frees an address previously allocated by Alloc().
func (a *Allocator) Free(addr uint64) {
	order, ok := a.spacemap[addr]
	if !ok {
		if glog.V(1) {
			glog.Errorf("Free: attempting to free address %#x, which was not previously allocated\n", addr)
		}
		return
	}

	if glog.V(2) {
		glog.Infof("Free: freeing %v bytes at address %#x\n", (1 << order), addr)
	}

	a.totalFree += (1 << order)
	delete(a.spacemap, addr)
	a.free(addr, order)
}

// 2-bit values representing the state of a particular address at a given level in the Allocator.
const (
	// The address is neither allocated nor free at this level.
	unknown byte = iota

	// The address is free at this level.
	available

	// The address has been allocated at this level.
	busy

	// Reserved for future use.  Currently if this value is encountered, the de-serializing code will
	// assume the data is corrupted.
	reserved
)

// Various constants for the encoded format.
const (
	// Size of the header.
	headerSize = 24

	// Offset and length of the size field in the header.
	sizeOff = 0
	sizeLen = 8

	// Offset and length of the free field in the header.
	freeOff = 8
	freeLen = 8

	// Offsets for the orderMin and orderMax bytes.
	orderMinOff = 16
	orderMaxOff = 17
)

// MarshalBinary implements the encoding.BinaryMarshaler interface for the Allocator.  All data is
// stored in little endian format.  The marshaled data begins with a header, formatted as follows:
//
// 0                                                                      64
// +-----------------------------------------------------------------------+
// |                                 size                                  |
// +-----------------------------------------------------------------------+
// |                                 free                                  |
// +--------+--------+-----------------------------------------------------+
// |orderMin|orderMax|                      reserved                       |
// +--------+--------+-----------------------------------------------------+
//
// The header is followed by one data entry for each order level in the buddy allocator.
// Each data entry is a two-bit array with one entry for every possible address at the given order level.
// These are the possible values for each entry and what they mean:
//
//    00 - This address is neither allocated nor free at this level.
//    01 - This address is currently free at this level.
//    10 - This address is currently in use at this level.
//    11 - Reserved for future use.
//
// The number of bytes used for each order level can be calculated using the formula
//
//                                a.size
//                            -----------------
//                             4 * (2 ^ order)
//
func (a *Allocator) MarshalBinary() ([]byte, error) {
	buf := make([]byte, a.encodedSize)

	// Fill in the header.  If this is changed in any way, the corresponding lines in
	// UnmarshalBinary() also need to be updated.
	binary.LittleEndian.PutUint64(buf[sizeOff:sizeOff+sizeLen], a.size)
	binary.LittleEndian.PutUint64(buf[freeOff:freeOff+freeLen], a.totalFree)
	buf[orderMinOff] = byte(uint8(a.orderMin))
	buf[orderMaxOff] = byte(uint8(a.orderMax))

	// Go through all the freemaps and populate the addresses at the appropriate levels.
	bitArrays := make([]bitops.TwoBitArray, len(a.freemaps))
	offset := headerSize
	for order := a.orderMin; order <= a.orderMax; order++ {
		idx := int(order - a.orderMin)
		size := int(a.size >> (order + 2)) // a.size / (4 * (2 ^ order))

		array := bitops.TwoBitArray(buf[offset : offset+size])
		for addr := range a.freemaps[idx] {
			array.Set(uint(addr>>order), available)
		}
		bitArrays[idx] = array
		offset += size
	}

	// Now go through the allocated map and mark those addresses as busy.
	for addr, order := range a.spacemap {
		idx := int(order - a.orderMin)

		bitArrays[idx].Set(uint(addr>>order), busy)
	}

	return buf, nil
}

// UnmarshalBinary implements the encoding.BinaryUnmarshaler interface for the Allocator.  See
// MarshalBinary for a description of the marshaled format.  UnmarshalBinary returns ErrCorrupted
// if the data does not follow the expected format.
func (a *Allocator) UnmarshalBinary(buf []byte) error {
	// Check the header.
	if len(buf) < headerSize {
		if glog.V(1) {
			glog.Error("UnmarshalBinary: buffer is too small for header")
		}
		return ErrCorrupted
	}

	a.size = binary.LittleEndian.Uint64(buf[sizeOff : sizeOff+sizeLen])
	a.totalFree = binary.LittleEndian.Uint64(buf[freeOff : freeOff+freeLen])
	if a.totalFree > a.size {
		if glog.V(1) {
			glog.Errorf("UnmarshalBinary: a.totalFree(%v) > a.size(%v)\n", a.totalFree, a.size)
		}
		return ErrCorrupted
	}

	a.orderMin = uint(buf[orderMinOff])
	a.orderMax = uint(buf[orderMaxOff])
	if a.orderMax <= a.orderMin {
		if glog.V(1) {
			glog.Errorf("UnmarshalBinary: a.orderMax(%v) <= a.orderMin(%v)\n", a.orderMax, a.orderMin)
		}
		return ErrCorrupted
	}

	// Figure out how big the rest of the buffer should be.
	a.calculateEncodedSize()
	if len(buf) < a.encodedSize {
		if glog.V(1) {
			glog.Errorf("UnmarshalBinary: len(buf) = %v; want %v\n", len(buf), a.encodedSize)
		}
		return ErrCorrupted
	}

	// Now we have enough information to start populating the maps.
	a.freemaps = make([]freemap, a.numFreeMaps())
	for i := range a.freemaps {
		a.freemaps[i] = make(freemap)
	}
	a.spacemap = make(map[uint64]uint)

	// Separately keep track of the free space and verify it at the end.
	offset := headerSize
	totalFree := uint64(0)
	for order := a.orderMin; order <= a.orderMax; order++ {
		idx := order - a.orderMin
		count := int(a.size >> order)
		size := count >> 2
		array := bitops.TwoBitArray(buf[offset : offset+size])

		for i := 0; i < count; i++ {
			switch array.Get(uint(i)) {
			case unknown:
			case available:
				totalFree += (1 << order)
				a.freemaps[idx][uint64(i)<<order] = true
			case busy:
				a.spacemap[uint64(i)<<order] = order
			case reserved:
				if glog.V(1) {
					glog.Errorf("UnmarshalBinary: entry %v at order %v has reserved value\n", i, order)
				}
				return ErrCorrupted
			default:
				if glog.V(1) {
					glog.Error("UnmarshalBinary: hit default case in switch statement")
				}
				return ErrCorrupted
			}
		}

		offset += size
	}

	if a.totalFree != totalFree {
		if glog.V(1) {
			glog.Errorf("UnmarshalBinary: a.totalFree = %v; want %v\n", a.totalFree, totalFree)
		}
		return ErrCorrupted
	}

	return nil
}
