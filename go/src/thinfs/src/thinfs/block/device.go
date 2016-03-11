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

	// WriteAt writes the contents of p to the devices starting at offset off.  Both off and len(p) must
	// be multiples of BlockSize().  It returns the number of bytes written and an error, if any.
	// WriteAt always returns a non-nil error when n < len(p).
	WriteAt(p []byte, off int64) (n int, err error)

	// Flush forces any writes that have been cached in memory to be committed to persistent storage.
	// Flush will not return until all data from previous calls to WriteAt have been committed to
	// stable storage.  Returns an error, if any.
	Flush() error

	// Close calls Flush() and then closes the device, rendering it unusable for I/O.  It returns an error,
	// if any.
	Close() error
}
