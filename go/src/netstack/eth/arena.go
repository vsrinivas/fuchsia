// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"fmt"
	"sync"
	"syscall/mx"
	"unsafe"
)

const numBuffers = 1024
const bufferSize = 2048

// A Buffer is a single packet-sized segment of non-heap memory.
//
// The memory is part of a VMO and is shared with a separate server process.
//
// A Buffer must be aquired from an Arena.
// A Buffer must not by appended to beyond its initial capacity.
// A Buffer may be sliced.
type Buffer []byte

// An Arena is a block of non-heap mmeory allocated in a VMO.
// Arenas are split into fixed-size Buffers, and are shared with
// ethernet drivers.
type Arena struct {
	iovmo mx.VMO
	iobuf uintptr

	mu       sync.Mutex
	freebufs []int
	owner    [numBuffers]*Client // nil means buffer is free
}

// NewArena creates a new Arena of fixed size.
func NewArena() (*Arena, error) {
	iosize := uint64(numBuffers * bufferSize)
	iovmo, err := mx.NewVMO(uint64(iosize), 0)
	if err != nil {
		return nil, fmt.Errorf("eth: cannot allocate I/O VMO: %v", err)
	}

	iobuf, err := mx.VMARRoot.Map(0, iovmo, 0, iosize, mx.VMFlagPermRead|mx.VMFlagPermWrite)
	if err != nil {
		iovmo.Close()
		return nil, fmt.Errorf("eth.Arena: I/O map failed: %v", err)
	}

	a := &Arena{
		iovmo:    iovmo,
		iobuf:    iobuf,
		freebufs: make([]int, numBuffers),
	}
	for i := 0; i < numBuffers; i++ {
		a.freebufs[i] = i
	}
	return a, nil
}

func (a *Arena) alloc(c *Client) Buffer {
	a.mu.Lock()
	defer a.mu.Unlock()
	if len(a.freebufs) == 0 {
		return nil
	}
	i := a.freebufs[len(a.freebufs)-1]
	a.freebufs = a.freebufs[:len(a.freebufs)-1]
	if a.owner[i] != nil {
		panic(fmt.Sprintf("eth.Arena: free list buffer %d is not free", i))
	}
	a.owner[i] = c
	return a.bufferLocked(i)
}

func (a *Arena) free(c *Client, b Buffer) {
	a.mu.Lock()
	defer a.mu.Unlock()
	i := a.indexLocked(b)
	if a.owner[i] != c {
		panic(fmt.Sprintf("eth.Arena: freeing a buffer owned by another client: %d (owner: %p, caller: %p)", i, a.owner[i], c))
	}
	a.owner[i] = nil
	a.freebufs = append(a.freebufs, i)
}

func (a *Arena) freeAll(c *Client) {
	a.mu.Lock()
	defer a.mu.Unlock()
	for i, owner := range a.owner {
		if owner == c {
			a.owner[i] = nil
			a.freebufs = append(a.freebufs, i)
		}
	}
}

func (a *Arena) indexLocked(b Buffer) int {
	p := uintptr(unsafe.Pointer(&b[:1][0]))
	i := int((p - a.iobuf) / bufferSize)
	if i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer 0x%x (len=%d, cap=%d) not in iobuf 0x%x", p, len(b), cap(b), a.iobuf))
	}
	return i
}

func (a *Arena) entry(b Buffer) bufferEntry {
	a.mu.Lock()
	defer a.mu.Unlock()
	i := a.indexLocked(b)
	p := uintptr(unsafe.Pointer(&b[:1][0]))
	return bufferEntry{
		offset: uint32(p - a.iobuf),
		length: uint16(len(b)),
		cookie: (cookieMagic << 32) | uintptr(i),
	}
}

func (a *Arena) bufferLocked(i int) (b Buffer) {
	if i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer index %d is out of range", i))
	}
	type sliceHeader struct {
		data unsafe.Pointer
		len  int
		cap  int
	}
	*(*sliceHeader)(unsafe.Pointer(&b)) = sliceHeader{
		data: unsafe.Pointer(a.iobuf + uintptr(i)*bufferSize),
		len:  bufferSize,
		cap:  bufferSize,
	}
	return b
}

func (a *Arena) bufferFromEntry(e bufferEntry) Buffer {
	a.mu.Lock()
	defer a.mu.Unlock()
	i := int(int32(e.cookie))
	if e.cookie>>32 != cookieMagic || i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer entry has bad cookie: %x", e.cookie))
	}
	isFree := a.owner[i] == nil
	if isFree {
		panic(fmt.Sprintf("eth: buffer entry %d is on free list", i))
	}
	b := a.bufferLocked(i)
	b = b[:e.length]
	return b
}

// bufferEntry is used to communicate a buffer over tx/rx fifos.
//
// The layout is known to ethernet drivers as eth_fifo_entry_t.
//
// In a departure from the magenta convention, we store a buffer index
// in cookie instead of a pointer.
type bufferEntry struct {
	offset uint32
	length uint16
	flags  uint16
	cookie uintptr // opaque void*
}

const cookieMagic = 0x42420102 // used to fill top 32-bits of bufferEntry.cookie
