// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"fmt"
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

	freebufs []int
	isFree   [numBuffers]bool
}

// NewArena creates a new Arena of fixed size.
func NewArena() (*Arena, error) {
	iosize := numBuffers * bufferSize
	iovmo, err := mx.NewVMO(uint64(iosize), 0)
	if err != nil {
		return nil, fmt.Errorf("eth: cannot allocate I/O VMO: %v", err)
	}
	var iobuf uintptr

	// TODO: cleaner VMAR handling in the mx package
	const MX_VM_FLAG_PERM_READ = 1 << 0
	const MX_VM_FLAG_PERM_WRITE = 1 << 1
	status := mx.Sys_vmar_map(mx.VmarRootHandle, 0, mx.Handle(iovmo), 0, uint(iosize), MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &iobuf)
	if status < 0 {
		iovmo.Close()
		return nil, fmt.Errorf("eth.Arena: I/O map failed: %v", err)
	}

	a := &Arena{
		iovmo:    iovmo,
		iobuf:    iobuf,
		freebufs: make([]int, numBuffers),
	}
	for i := 0; i < numBuffers; i++ {
		a.isFree[i] = true
		a.freebufs[i] = i
	}
	return a, nil
}

func (a *Arena) alloc() Buffer {
	if len(a.freebufs) == 0 {
		return nil
	}
	i := a.freebufs[len(a.freebufs)-1]
	a.freebufs = a.freebufs[:len(a.freebufs)-1]
	if !a.isFree[i] {
		panic(fmt.Sprintf("eth.Arena: free list buffer %d is not free", i))
	}
	a.isFree[i] = false
	return a.buffer(i)
}

func (a *Arena) free(b Buffer) {
	i := a.index(b)
	if a.isFree[i] {
		panic(fmt.Sprintf("eth.Arena: freeing a free buffer: %d", i))
	}
	a.isFree[i] = true
	a.freebufs = append(a.freebufs, i)
}

func (a *Arena) index(b Buffer) int {
	p := uintptr(unsafe.Pointer(&b[:1][0]))
	i := int((p - a.iobuf) / bufferSize)
	if i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer 0x%x (len=%d, cap=%d) not in iobuf 0x%x", p, len(b), cap(b), a.iobuf))
	}
	return i
}

func (a *Arena) entry(b Buffer) bufferEntry {
	i := a.index(b)
	p := uintptr(unsafe.Pointer(&b[:1][0]))
	return bufferEntry{
		offset: uint32(p - a.iobuf),
		length: uint16(len(b)),
		cookie: (cookieMagic << 32) | uintptr(i),
	}
}

func (a *Arena) buffer(i int) (b Buffer) {
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
	i := int(int32(e.cookie))
	if e.cookie>>32 != cookieMagic || i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer entry has bad cookie: %x", e.cookie))
	}
	isFree := a.isFree[i]
	if isFree {
		panic(fmt.Sprintf("eth: buffer entry %d is on free list", i))
	}
	b := a.buffer(i)
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
