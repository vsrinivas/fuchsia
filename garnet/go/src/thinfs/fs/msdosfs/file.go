// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package msdosfs

import (
	"sync"
	"time"

	"thinfs/fs"
	"thinfs/fs/msdosfs/node"
)

// Structure representing position within file. May be shared between multiple dup-ed files.
type filePosition struct {
	sync.Mutex
	offset int64 // Seek position in the file
}

type file struct {
	fs       *fsFAT
	node     node.FileNode
	flags    fs.OpenFlags  // Original flags which opened this file
	position *filePosition // Position within file. Used for next read / write, depending on whence.

	sync.RWMutex
	closed bool
}

// Ensure the file implements the fs.File interface
var _ fs.File = (*file)(nil)

func (f *file) Close() error {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return fs.ErrUnmounted
	}
	f.Lock() // Changing 'closed'
	defer f.Unlock()
	if f.closed {
		return fs.ErrNotOpen
	}
	f.closed = true

	return closeFile(f.node)
}

func (f *file) Stat() (int64, time.Time, time.Time, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return 0, time.Time{}, time.Time{}, fs.ErrUnmounted
	}
	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return 0, time.Time{}, time.Time{}, fs.ErrNotOpen
	}

	return stat(f.node)
}

func (f *file) Touch(lastAccess, lastModified time.Time) error {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return fs.ErrUnmounted
	}
	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return fs.ErrNotOpen
	}

	touch(f.node, lastAccess, lastModified)
	return nil
}

func (f *file) Dup() (fs.File, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return nil, fs.ErrUnmounted
	}
	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return nil, fs.ErrNotOpen
	}

	dup(f.node)
	return &file{
		fs:       f.fs,
		node:     f.node,
		flags:    f.flags,
		position: f.position,
	}, nil
}

func (f *file) GetOpenFlags() fs.OpenFlags {
	f.RLock()
	defer f.RUnlock()
	return f.flags
}

func (f *file) SetOpenFlags(flags fs.OpenFlags) error {
	f.Lock()
	defer f.Unlock()
	f.flags = flags
	return nil
}

// TODO(smklein): Test reopen before plugging it into Remote IO
func (f *file) Reopen(flags fs.OpenFlags) (fs.File, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return nil, fs.ErrUnmounted
	}
	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return nil, fs.ErrNotOpen
	}

	if (flags.Read() && !f.flags.Read()) || (flags.Write() && !f.flags.Write()) {
		return nil, fs.ErrPermission
	} else if flags.Directory() {
		return nil, fs.ErrNotADir
	}

	dup(f.node)
	return &file{
		fs:       f.fs,
		node:     f.node,
		flags:    flags,
		position: &filePosition{}, // Create a new file position (unlike dup)
	}, nil
}

func (f *file) Read(p []byte, off int64, whence int) (int, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return 0, fs.ErrUnmounted
	}
	f.RLock() // Reading 'closed'
	defer f.RUnlock()
	if f.closed {
		return 0, fs.ErrNotOpen
	}

	if !f.flags.Read() {
		return 0, fs.ErrPermission
	}

	f.node.RLock()
	defer f.node.RUnlock()

	var updateCursor bool
	switch whence {
	case fs.WhenceFromCurrent:
		f.position.Lock()
		defer f.position.Unlock()
		off += f.position.offset
		updateCursor = true
	case fs.WhenceFromStart:
	case fs.WhenceFromEnd:
		off += f.node.Size()
	default:
		return 0, fs.ErrInvalidArgs
	}

	bytesRead, err := f.node.ReadAt(p, off)
	if updateCursor {
		f.position.offset = off + int64(bytesRead)
	}
	return bytesRead, err
}

func (f *file) Write(p []byte, off int64, whence int) (int, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return 0, fs.ErrUnmounted
	}

	f.RLock() // Reading 'closed'
	defer f.RUnlock()
	if f.closed {
		return 0, fs.ErrNotOpen
	}

	if f.fs.info.Readonly {
		return 0, fs.ErrPermission
	} else if !f.flags.Write() {
		return 0, fs.ErrPermission
	}

	f.node.Lock()
	defer f.node.Unlock()

	var updateCursor bool
	if f.flags.Append() {
		// The 'append' flag is the same as forcing 'WhenceFromEnd' on all writes
		whence = fs.WhenceFromEnd
		f.position.Lock()
		defer f.position.Unlock()
		updateCursor = true
	}

	switch whence {
	case fs.WhenceFromCurrent:
		f.position.Lock()
		defer f.position.Unlock()
		updateCursor = true
		off += f.position.offset
	case fs.WhenceFromStart:
	case fs.WhenceFromEnd:
		off += f.node.Size()
	default:
		return 0, fs.ErrInvalidArgs
	}

	bytesWritten, err := f.node.WriteAt(p, off)
	if updateCursor {
		f.position.offset = off + int64(bytesWritten)
	}
	return bytesWritten, err
}

func (f *file) Truncate(size uint64) error {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return fs.ErrUnmounted
	}

	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return fs.ErrNotOpen
	}

	if f.fs.info.Readonly || !f.flags.Write() {
		return fs.ErrPermission
	}

	if size > uint64(node.MaxSizeFile) {
		return fs.ErrInvalidArgs
	}

	f.node.Lock()
	defer f.node.Unlock()
	if size > uint64(f.node.Size()) {
		empty := make([]byte, 1<<14)
		for size > uint64(f.node.Size()) {
			// Make the file larger (filled with zeroes). Since FAT (unfortunately) does not support
			// sparse files, this requires manually writing 'zero' to the end of the file.
			bytesToWrite := uint64(len(empty))
			bytesLeft := size - uint64(f.node.Size())
			if bytesToWrite > bytesLeft {
				bytesToWrite = bytesLeft
			}
			f.node.WriteAt(empty[:bytesToWrite], f.node.Size())
		}
	}
	if size < uint64(f.node.Size()) {
		// Make the file smaller. This can be done by simply reducing the size.
		f.node.SetSize(int64(size))
	}
	return nil
}

func (f *file) Tell() (int64, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return 0, fs.ErrUnmounted
	}

	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return 0, fs.ErrNotOpen
	}

	f.position.Lock()
	defer f.position.Unlock()
	return f.position.offset, nil
}

func (f *file) Seek(offset int64, whence int) (int64, error) {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return 0, fs.ErrUnmounted
	} else if f.flags.Path() {
		return 0, fs.ErrPermission
	}

	f.RLock() // Reading 'closed'
	defer f.RUnlock()
	if f.closed {
		return 0, fs.ErrNotOpen
	}

	f.node.RLock() // Possibly reading 'Size'
	defer f.node.RUnlock()
	f.position.Lock() // Setting position
	defer f.position.Unlock()

	switch whence {
	case fs.WhenceFromCurrent:
		f.position.offset += offset
	case fs.WhenceFromStart:
		f.position.offset = offset
	case fs.WhenceFromEnd:
		f.position.offset = f.node.Size() + offset
	default:
		return 0, fs.ErrInvalidArgs
	}

	return f.position.offset, nil
}

func (f *file) Sync() error {
	f.fs.RLock()
	defer f.fs.RUnlock()
	if f.fs.unmounted {
		return fs.ErrUnmounted
	}
	f.RLock()
	defer f.RUnlock()
	if f.closed {
		return fs.ErrNotOpen
	}

	return syncFile(f.node)
}
