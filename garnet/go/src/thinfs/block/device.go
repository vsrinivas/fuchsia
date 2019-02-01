// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package block defines the interface that all block-based devices must present to ThinFS.
package block

// Device is the interface that all block-based devices must present to ThinFS.
type Device interface {
	// BlockSize returns the size in bytes of the smallest block that the device can read
	// or write at once.  The return value is undefined after Close() is called.
	BlockSize() int64

	// Size returns the fixed size of the device in bytes.  The return value is undefined after
	// Close() is called.
	Size() int64

	// ReadAt reads len(p) bytes from the device starting at offset off.  Both off and len(p) must
	// be multiples of BlockSize().  It returns the number of bytes read and the error, if any.
	// ReadAt always returns a non-nil error when n < len(p).
	ReadAt(p []byte, off int64) (n int, err error)

	// WriteAt writes the contents of p to the device starting at offset off.  Both off and len(p) must
	// be multiples of BlockSize().  It returns the number of bytes written and an error, if any.
	// WriteAt always returns a non-nil error when n < len(p).
	WriteAt(p []byte, off int64) (n int, err error)

	// Flush forces any writes that have been cached in memory to be committed to persistent storage.
	// Flush will not return until all data from previous calls to WriteAt have been committed to
	// stable storage.  Returns an error, if any.
	Flush() error

	// Discard marks the address range [off, off+len) as being unused, allowing it to be reclaimed by
	// the device for other purposes.  Both off and len must be multiples of BlockSize().  Returns an
	// error, if any.
	Discard(off, len int64) error

	// Close calls Flush() and then closes the device, rendering it unusable for I/O.  It returns an error,
	// if any.
	Close() error

	// Path returns the path to the underlying block device.
	Path() string
}
