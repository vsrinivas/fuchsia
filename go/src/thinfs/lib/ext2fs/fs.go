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

// Package ext2fs provides a cgo wrapper around libext2fs from the e2fsprogs library.
package ext2fs

// #cgo pkg-config: ext2fs
// #include <ext2fs.h>
// #include <ext2_io.h>
// #include <stdlib.h>
import "C"

import (
	"fmt"
	"runtime"
	"unsafe"

	"github.com/golang/glog"
)

// FS represents an ext2 file system.
type FS struct {
	fs C.ext2_filsys
}

// New creates a new instance of an ext2 filesystem.  |path| should be the path to the filesystem image.
func New(path string) (*FS, error) {
	f := &FS{}

	p := C.CString(path)
	defer C.free(unsafe.Pointer(p))

	if err := check(C.ext2fs_open(p, 0, 0, 0, C.unix_io_manager, &f.fs)); err != nil {
		return nil, fmt.Errorf("error opening ext2 file system: %v", err)
	}

	// Populate the inode bitmap so that we know which inodes are in use.
	if err := check(C.ext2fs_read_inode_bitmap(f.fs)); err != nil {
		return nil, fmt.Errorf("error reading inode bitmap: %v", err)
	}

	// Populate the block bitmap so that we know which blocks are in use.
	if err := check(C.ext2fs_read_block_bitmap(f.fs)); err != nil {
		return nil, fmt.Errorf("error reading block bitmap: %v", err)
	}

	runtime.SetFinalizer(f, func() {
		glog.Errorf("File system image at %s became unreachable before it was closed\n", path)
		if err := f.Close(); err != nil {
			glog.Errorf("Error closing file system at %s: %v", path, err)
		}
	})
	return f, nil
}

// Close closes the file system and cleans up any memory used by it.  To prevent memory leaks, it
// is required for callers to call Close before converting the FS object into garbage for the GC.
// Returns an error, if any.
func (f *FS) Close() error {
	defer func() {
		C.ext2fs_free(f.fs)
		f.fs = nil
	}()

	runtime.SetFinalizer(f, nil)
	if err := check(C.ext2fs_close(f.fs)); err != nil {
		return err
	}
	return nil
}

// FirstNonReservedInode returns the inode number for the first non-reserved inode in the file system.
func (f *FS) FirstNonReservedInode() Inum {
	return Inum(f.fs.super.s_first_ino)
}

// Blocksize returns the size of a single block (in bytes) in the file system.
func (f *FS) Blocksize() int64 {
	return int64(f.fs.blocksize)
}

// Blockcount return the total number of blocks in the file system.
func (f *FS) Blockcount() int64 {
	return int64(f.fs.super.s_blocks_count)
}

// Size returns the full size of the file system.  Equivalent to f.Blocksize() * f.Blockcount().
func (f *FS) Size() int64 {
	return f.Blocksize() * f.Blockcount()
}

// ForEachInode allows callers to iterate over every inode in the file system.  |do| is a function
// that will be invoked with every inode passed in as a parameter.  |do| must return true to continue
// iteration or return false to stop iteration.
func (f *FS) ForEachInode(do func(*Inode) bool) error {
	var iscan C.ext2_inode_scan

	if err := check(C.ext2fs_open_inode_scan(f.fs, 0 /* buffer_blocks */, &iscan)); err != nil {
		return fmt.Errorf("error opening inode scan: %v", err)
	}

	for {
		inode := &Inode{fs: f.fs}
		if err := check(C.ext2fs_get_next_inode(iscan, &inode.num, &inode.i)); err != nil {
			return fmt.Errorf("error while scanning inodes: %v", err)
		}

		if inode.num == 0 {
			// We've reached the end of the scan.
			return nil
		}

		if !do(inode) {
			// The caller has requested early terimination.
			return nil
		}
	}
}

// ForEachBlock allows callers to iterate over every block in the file system.  |do| is a function
// that will be invoked with every block passed in as a parameter.  |do| must return true to continue
// iteration or return false to stop iteration.
func (f *FS) ForEachBlock(do func(*Block) bool) {
	start := C.ext2fs_get_block_bitmap_start(f.fs.block_map)
	end := C.ext2fs_get_block_bitmap_end(f.fs.block_map)

	for s := start; s < end; s++ {
		if !do(&Block{fs: f.fs, num: s}) {
			break
		}
	}
}
