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
import "C"

// A Block represents a single block in the ext2 file system.
type Block struct {
	fs   C.ext2_filsys // The file system that owns this block.
	num  C.blk_t       // The block number.
	lnum C.e2_blkcnt_t // The logical block number for this block.
}

// InUse returns true iff the block is actively being used by the file system.
func (b *Block) InUse() bool {
	return C.ext2fs_test_block_bitmap(b.fs.block_map, b.num) != 0
}

// Number returns the block number in the file system.
func (b *Block) Number() int64 {
	return int64(b.num)
}

// LogicalNumber returns the logical block number in the inode to which the block belongs.
func (b *Block) LogicalNumber() int64 {
	return int64(b.lnum)
}

// IsIndirectBlock returns true iff the block is an indirect block for an inode.
func (b *Block) IsIndirectBlock() bool {
	return b.lnum == C.BLOCK_COUNT_IND || b.lnum == C.BLOCK_COUNT_DIND || b.lnum == C.BLOCK_COUNT_TIND
}
