// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package iomisc

import (
	"io"
)

// readerAtAdapter wraps an io.ReaderAt and implements io.Reader.
type readerAtAdapter struct {
	ra     io.ReaderAt
	offset int64
}

// Read reads bytes into b starting at the first unread byte.
func (r *readerAtAdapter) Read(b []byte) (int, error) {
	n, err := r.ra.ReadAt(b, r.offset)
	r.offset += int64(n)
	return n, err
}

// ReaderAtToReader creates a new Reader from a ReaderAt.
func ReaderAtToReader(r io.ReaderAt) io.Reader {
	return &readerAtAdapter{
		ra:     r,
		offset: 0,
	}
}
