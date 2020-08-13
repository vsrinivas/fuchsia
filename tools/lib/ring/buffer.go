// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package ring gives a simple implementation of a ring/b.bufular buffer.
package ring

import (
	"fmt"
	"io"
	"runtime"
	"sync"
)

// Buffer is a simple implementation of a ring/b.bufular buffer.
// See https://en.wikipedia.org/wiki/b.bufular_buffer for more details.
type Buffer struct {
	sync.Mutex
	buf []byte
	// Picture [read, write] as a sliding window. These values grow without
	// bounds, read not being allowed to exceed write, but are handled
	// modularly during |buf| i/o considerations.
	read  int
	write int
}

// NewBuffer returns a ring buffer of a given size.
func NewBuffer(size int) *Buffer {
	if size <= 0 {
		panic(fmt.Sprintf("size was %d; must be positive", size))
	}
	return &Buffer{
		buf: make([]byte, size),
	}
}

// Read reads from the buffer, returning io.EOF if it has read up until where
// it has written.
func (b *Buffer) Read(p []byte) (int, error) {
	b.Lock()
	defer b.Unlock()

	maxBytes := min(len(p), b.write-b.read)
	b.copyToBuffer(p[:maxBytes], b.read)
	b.read += maxBytes
	if maxBytes == 0 {
		return 0, io.EOF
	} else {
		return maxBytes, nil
	}
}

// Write writes to the buffer, b.bufularly overwriting data if p exceeds the
// size of the buffer.
func (b *Buffer) Write(p []byte) (int, error) {
	total := len(p)
	for {
		if len(p) == 0 {
			break
		}
		// Before we overwrite data, preempt the current goroutine to allow all
		// other current ones - which might be waiting to read - execute first so
		// as to minimize data loss.
		runtime.Gosched()
		b.Lock()

		// We don't want b.write to get more then len(b.buf) ahead of b.read; we
		// read as much as possible taking that into account.
		maxBytes := min(len(p), len(b.buf)-(b.write-b.read))
		// If b.write and b.read are maximally far apart, we can overwrite
		// len(p) or len(b.buf) many bytes.
		if maxBytes == 0 {
			maxBytes = min(len(p), len(b.buf))
			b.read += maxBytes
		}
		b.copyFromBuffer(p[:maxBytes], b.write)
		b.write += maxBytes
		p = p[maxBytes:]
		b.Unlock()
	}
	return total, nil
}

// Bytes returns the number of unread bytes in the buffer.
func (b Buffer) Bytes() []byte {
	b.Lock()
	defer b.Unlock()
	p := make([]byte, b.write-b.read)
	b.copyToBuffer(p, b.read)
	return p
}

func (b *Buffer) copyToBuffer(p []byte, start int) {
	N := len(b.buf)
	P := len(p)

	// Assume P <= N.
	if P > N {
		panic("copyToBuffer: expects len(p) <= size of Buffer")
	}

	start = start % N
	if start+P <= N {
		copy(p, b.buf[start:P+start])
	} else {
		copy(p[:N-start], b.buf[start:])
		copy(p[N-start:], b.buf[:P-(N-start)])
	}
}

func (b *Buffer) copyFromBuffer(p []byte, start int) {
	N := len(b.buf)
	P := len(p)

	// Assume P <= N.
	if P > N {
		panic("copyFromBuffer: expects len(p) <= size of Buffer")
	}

	start = start % N
	if start+P <= N {
		copy(b.buf[start:start+P], p)
	} else {
		copy(b.buf[start:], p[:N-start])
		copy(b.buf[:P-(N-start)], p[N-start:])
	}
}

func min(n, m int) int {
	if n <= m {
		return int(n)
	}
	return m
}
