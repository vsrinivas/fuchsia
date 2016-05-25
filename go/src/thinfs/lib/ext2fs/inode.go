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

// #include <ext2fs.h>
import "C"

import (
	"time"

	"github.com/golang/glog"
	"github.com/pkg/errors"
)

// inode represents an inode in the file system.
type inode struct {
	fs  C.ext2_filsys // The file system to which this inode belongs.
	ino C.ext2_ino_t  // This inode's number.
}

func newInode(fs C.ext2_filsys, ino C.ext2_ino_t) inode {
	out := inode{fs, ino}
	inoRefCount[out]++
	return out
}

func (i inode) maybeFreeBlocks() error {
	var node C.struct_ext2_inode

	if err := check(C.ext2fs_read_inode(i.fs, i.ino, &node)); err != nil {
		return errors.Wrap(err, "unable to read inode")
	}

	if node.i_links_count > 0 {
		// There are still other links to this inode so we don't need to free the blocks
		// and since we didn't make any changes we don't need to write it out either.
		return nil
	}

	if err := freeBlocks(i.fs, i.ino, &node); err != nil {
		return err
	}

	if err := check(C.ext2fs_write_inode(i.fs, i.ino, &node)); err != nil {
		return errors.Wrap(err, "unable to write inode")
	}

	return nil
}

// Close closes the file or directory, decrements the active reference count for it, and frees
// the blocks pointed to by the file if there are no more active references to it and it
// is no longer linked to by any directory.  Returns an error, if any.  The returned error is
// purely informational and the close must not be retried.
func (i *inode) Close() error {
	if glog.V(2) {
		glog.Info("Close: ino=", i.ino)
	}

	// Copy over the old inode and then clear it so that we don't have double-free problems.
	old := *i
	*i = inode{}

	if _, ok := inoRefCount[old]; !ok {
		return errors.Wrapf(ErrNotOpen, "inode number %v", old.ino)
	}

	if inoRefCount[old]--; inoRefCount[old] > 0 {
		return nil
	}

	delete(inoRefCount, old)
	return old.maybeFreeBlocks()
}

func freeBlocks(fs C.ext2_filsys, ino C.ext2_ino_t, node *C.struct_ext2_inode) error {
	if C.ext2fs_inode_has_valid_blocks2(fs, node) != 0 {
		if err := check(C.ext2fs_punch(fs, ino, node, nil, 0, ^C.blk64_t(0))); err != nil {
			return errors.Wrap(err, "unable to free blocks")
		}
	}

	var isDir C.int
	if modeToFileType(node.i_mode) == C.EXT2_FT_DIR {
		isDir = 1
	}
	C.ext2fs_inode_alloc_stats2(fs, ino, -1 /* inUse */, isDir)

	return nil
}

func updateAtime(fs C.ext2_filsys, ino C.ext2_ino_t) error {
	var node C.struct_ext2_inode

	if err := check(C.ext2fs_read_inode(fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to read inode")
	}

	now := C.__u32(time.Now().Unix() & int64(^uint32(0))) // Only care about the lower 32-bits

	// This check is the equivalent of the relatime option.
	if node.i_atime <= node.i_mtime || node.i_atime < now-30 {
		node.i_atime = now
	}

	if err := check(C.ext2fs_write_inode(fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to write inode")
	}

	return nil
}

func updateCtime(fs C.ext2_filsys, ino C.ext2_ino_t) error {
	var node C.struct_ext2_inode

	if err := check(C.ext2fs_read_inode(fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to read inode")
	}

	now := C.__u32(time.Now().Unix() & int64(^uint32(0))) // Only care about the lower 32-bits
	node.i_ctime = now

	if err := check(C.ext2fs_write_inode(fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to write inode")
	}

	return nil
}

func updateMtime(fs C.ext2_filsys, ino C.ext2_ino_t) error {
	var node C.struct_ext2_inode

	if err := check(C.ext2fs_read_inode(fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to read inode")
	}

	now := C.__u32(time.Now().Unix() & int64(^uint32(0))) // Only care about the lower 32-bits

	// Updating the mtime should also update the ctime.
	node.i_mtime = now
	node.i_ctime = now

	if err := check(C.ext2fs_write_inode(fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to write inode")
	}

	return nil
}
