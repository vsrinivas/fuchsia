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

// FileType describes the type of a given file.
type FileType int

// Ext2 file types.  This order matches the order defined in ext2_fs.h and should not
// be changed.
const (
	Unknown FileType = iota
	RegularFile
	Directory
	CharacterDevice
	BlockDevice
	Fifo
	Socket
	Symlink
)

// Dirent represents a directory entry in an ext2 file system.
type Dirent C.struct_ext2_dir_entry

// Inum returns the inode number for the directory entry.
func (d *Dirent) Inum() Inum {
	return Inum(d.inode)
}

// Len returns the length of the directory entry.
func (d *Dirent) Len() int {
	return int(d.rec_len)
}

// Name returns the file name of the directory entry.
func (d *Dirent) Name() string {
	return C.GoStringN(&d.name[0], (C.int)(d.name_len&0xff))
}

// Type returns the file type for the directory entry.
func (d *Dirent) Type() FileType {
	return FileType(d.name_len >> 8)
}
