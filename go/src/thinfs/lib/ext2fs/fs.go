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

//go:generate ./build_libs.sh

// #cgo CFLAGS: -I${SRCDIR}/third_party/e2fsprogs/lib/et -I${SRCDIR}/third_party/e2fsprogs/lib/ext2fs
// #cgo LDFLAGS: -L${SRCDIR}/third_party/e2fsprogs/lib -lext2fs -lcom_err
// #include <ext2fs.h>
// #include <ext2_io.h>
// #include <stdlib.h>
import "C"

import (
	"runtime"
	"unsafe"

	"github.com/golang/glog"
	"github.com/pkg/errors"
)

// Options define the options that a client can request when opening a file system.
type Options int

const (
	// ReadOnly indicates that the file system should be opened only for reading.
	ReadOnly Options = 1 << iota

	// ReadWrite indicates that the file system should be opened for reading as well as
	// writing.
	ReadWrite

	// Force indicates that the file system should be opened regardless of the feature
	// sets listed in the superblock.
	Force
)

// inoRefCount keeps track of which inodes are currently actively in use.
var inoRefCount = make(map[inode]int)

func init() {
	C.initialize_ext2_error_table()
}

// FS represents an ext2 file system.
type FS struct {
	fs   C.ext2_filsys
	opts Options
}

// New creates a new instance of an ext2 filesystem.  |path| should be the path to the filesystem image.
func New(path string, opts Options) (*FS, error) {
	f := &FS{opts: opts}

	p := C.CString(path)
	defer C.free(unsafe.Pointer(p))

	var flags C.int
	if opts&ReadWrite != 0 {
		flags |= C.EXT2_FLAG_RW
	}
	if opts&Force != 0 {
		flags |= C.EXT2_FLAG_FORCE
	}
	// ioManager comes from one of the manager_*.go files.
	if err := check(C.ext2fs_open(p, flags, 0, 0, ioManager, &f.fs)); err != nil {
		return nil, errors.Wrap(err, "unable to open ext2 file system")
	}
	runtime.SetFinalizer(f, func(*FS) {
		glog.Errorf("File system image at %s became unreachable before it was closed\n", path)
		if err := f.Close(); err != nil {
			glog.Error(err)
		}
	})

	// Populate the inode and block bitmaps so that we know which inodes and blocks are in use.
	if err := check(C.ext2fs_read_bitmaps(f.fs)); err != nil {
		f.Close()
		return nil, errors.Wrap(err, "unable to read bitmaps")
	}

	return f, nil
}

// Close closes the file system and cleans up any memory used by it.  To prevent memory leaks, it
// is required for callers to call Close before converting the FS object into garbage for the GC.
// The returned error is informational only and the Close may not be retried.
func (f *FS) Close() error {
	runtime.SetFinalizer(f, nil)

	var outerr error
	if f.opts&ReadWrite != 0 {
		if f.fs.super.s_error_count != 0 {
			f.fs.super.s_state |= C.EXT2_ERROR_FS
		} else if len(inoRefCount) > 0 {
			glog.Warning("File system is being closed while there are still active references")
			f.fs.super.s_state |= C.EXT2_ERROR_FS
		} else {
			f.fs.super.s_state |= C.EXT2_VALID_FS
		}
		C.ext2fs_mark_super_dirty(f.fs)

		if err := check(C.ext2fs_set_gdt_csum(f.fs)); err != nil {
			outerr = errors.Wrap(err, "unable to set group descriptor table checksum")
		}
		if err := check(C.ext2fs_flush(f.fs)); err != nil && outerr == nil {
			// Only set the error if the previous operation was successful.
			outerr = errors.Wrap(err, "unable to flush file system")
		}
	}
	if err := check(C.ext2fs_close(f.fs)); err != nil && outerr == nil {
		outerr = errors.Wrap(err, "unable to close file system")
	}

	C.ext2fs_free(f.fs)
	f.fs = nil
	return outerr
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

// RootDirectory returns the root directory for the file system.  Callers must close the returned
// directory to ensure that all changes persist to disk.
func (f *FS) RootDirectory() *Dir {
	return &Dir{newInode(f.fs, C.EXT2_ROOT_INO)}
}
