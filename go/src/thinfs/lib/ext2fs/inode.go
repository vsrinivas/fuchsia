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

package ext2fs

// #cgo pkg-config: ext2fs
// #include <ext2fs.h>
//
// // Dealing with callbacks between C and Go is a bit hairy.  You usually need to typedef
// // the function pointer and then cast a C function to that pointer type and then it's all
// // good.  However, libext2fs doesn't have function pointer typedefs because it just directly
// // declares the callback in the parameter list of the function, which causes Go to think
// // that the parameter has a really weird type.  To actually make this work we have to jump
// // through a few hoops.
// //
// // First, we have the extern callback functions.  These are actually Go functions declared
// // extern in the C code and then exported from Go using the //extern directive.  These
// // functions match the signature expected from the callback function.
// //
// // Next we have the static inline functions.  These are called from Go code and take some
// // necessary arguments.  All they do is forward those arguments to the library function while
// // also passing in the extern callback functions from the previous step.
// //
// // The library function then does its thing and eventually calls the callback function, which
// // trampolines back up to the Go code.  The final order looks like:
// //
// //   Go code -> static inline function -> library function -> extern function -> Go code
// //
// // There's some extra wrinkles because we also need to pass around a handle as private
// // data so that the callback function can actually do something meaningful, but we can just
// // stop here for now.
// extern int blockIterCB(ext2_filsys fs, blk_t *blocknr, e2_blkcnt_t blockcnt, blk_t ref_blk,
//                        int ref_offset, void *priv);
//
// static inline errcode_t block_iterate(ext2_filsys fs, ext2_ino_t inum, int flags, void *priv) {
//   return ext2fs_block_iterate2(fs, inum, flags, NULL, blockIterCB, priv);
// }
//
// extern int dirIterCB(struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *priv);
//
// static inline errcode_t dir_iterate(ext2_filsys fs, ext2_ino_t dir, int flags, void *priv) {
//   return ext2fs_dir_iterate(fs, dir, flags, NULL, dirIterCB, priv);
// }
import "C"

import (
	"errors"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/handle"
)

// BlockFlag represents flags that are used to control the behavior of the block iterator.
type BlockFlag int

// Block iterator flags.  The order of these flags must not be changed.
const (
	// Indicates that blocks where the block number is zero should be included.
	Hole BlockFlag = 1 << iota

	// Indicates that the indirect, doubly indirect, and triply indirect should be included
	// _after_ all the blocks contained in the indirect blocks.
	DepthTraverse

	// Indicates that only data blocks should be included (i.e. no indirect blocks).
	DataOnly

	// Indicates that the caller will not modify the returned block.
	ReadOnly
)

// DirentFlag represents flags that are used to control the behavior of the directory entry
// iterator.
type DirentFlag int

// Directory iterator flags.  The order of these flags must not be changed.
const (
	// Indicates that empty directories should be included.
	IncludeEmpty DirentFlag = 1 << iota

	// Indicates that removed directories should be included.
	IncludeRemoved
)

// ErrNotADirectory is the error returns if ForEachDirent is called on an inode that is not
// a directory.
var ErrNotADirectory = errors.New("inode is not a directory")

// Inum represents an inode number.
type Inum C.ext2_ino_t

// Inode represents an inode in the file system.
type Inode struct {
	fs  C.ext2_filsys       // The file system to which this inode belongs.
	num C.ext2_ino_t        // This inode's number.
	i   C.struct_ext2_inode // The actual inode struct.
}

// InUse returns true iff the inode is actively being used by the file system.
func (i *Inode) InUse() bool {
	return C.ext2fs_test_inode_bitmap(i.fs.inode_map, i.num) != 0
}

// HasValidBlocks return true iff the inode has valid blocks in use by the file system.
func (i *Inode) HasValidBlocks() bool {
	return C.ext2fs_inode_has_valid_blocks(&i.i) != 0
}

// IsDirectory returns true iff the inode holds a directory.
func (i *Inode) IsDirectory() bool {
	return C.ext2fs_check_directory(i.fs, i.num) == 0
}

// Number returns the inode number in the file system.
func (i *Inode) Number() uint32 {
	return uint32(i.num)
}

// Pathname returns the file name for the inode.
func (i *Inode) Pathname() (string, error) {
	var name *C.char

	defer C.ext2fs_free_mem(unsafe.Pointer(&name))
	if err := check(C.ext2fs_get_pathname(i.fs, i.num, 0, &name)); err != nil {
		return "", err
	}

	return C.GoString(name), nil
}

// blockIterCB is the counter-part to the extern C function with the same name declared at the
// top of this file.  We use the export keyword to make it callable from C code.
//export blockIterCB
func blockIterCB(fs C.ext2_filsys, blocknr *C.blk_t, blockcnt C.e2_blkcnt_t, _ C.blk_t, _ C.int, priv unsafe.Pointer) C.int {
	fn := handle.MustValue(uintptr(priv)).(func(*Block) bool)

	if !fn(&Block{fs, *blocknr, blockcnt}) {
		return C.BLOCK_ABORT
	}

	return 0
}

// ForEachBlock iterates over every block that the inode holds.  |flags| should be a
// bitwise OR of the BlockFlag constants declared in this file.  |do| will be called once
// for every block in the inode and must return true to continue iteration or return false
// to stop iteration.
func (i *Inode) ForEachBlock(flags BlockFlag, do func(*Block) bool) error {
	priv := handle.New(do)
	defer handle.MustDelete(priv)

	// This call will eventually make its way back to blockIterCB above.
	if err := check(C.block_iterate(i.fs, i.num, C.int(flags), priv)); err != nil {
		return err
	}

	return nil
}

// dirIterCB is the counter-part to the extern C function with the same name declared at the
// top of this file.  We use the export keyword to make it callable from C code.
//export dirIterCB
func dirIterCB(dirent *C.struct_ext2_dir_entry, _ C.int, _ C.int, _ *C.char, priv unsafe.Pointer) C.int {
	fn := handle.MustValue(uintptr(priv)).(func(*Dirent) bool)

	if !fn((*Dirent)(dirent)) {
		return C.DIRENT_ABORT
	}

	return 0
}

// ForEachDirent iterates over every directory entry that the inode holds.  |flags| should
// be a bitwise OR of the DirentFlag constants declared in this file.  |do| will be called once
// for every directory entry in the inode and must return true to continue iteration or return
// false to stop iteration.  Returns an error if the inode is not a directory.
func (i *Inode) ForEachDirent(flags DirentFlag, do func(*Dirent) bool) error {
	if !i.IsDirectory() {
		return ErrNotADirectory
	}

	priv := handle.New(do)
	defer handle.MustDelete(priv)

	// This will make its way back to dirIterCB above.
	if err := check(C.dir_iterate(i.fs, i.num, C.int(flags), priv)); err != nil {
		return err
	}

	return nil
}
