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
// // There's some extra wrinkles because we also need to pass around a C pointer as private
// // data so that the callback function can actually do something meaningful, but we can just
// // stop here for now.
// extern int dirIterCB(struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *priv);
//
// static inline errcode_t dir_iterate(ext2_filsys fs, ext2_ino_t dir, int flags, void *priv) {
//   return ext2fs_dir_iterate(fs, dir, flags, NULL, dirIterCB, priv);
// }
import "C"
import (
	"fmt"
	"path"
	"time"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/cpointer"
	"github.com/golang/glog"
	"github.com/pkg/errors"
)

// FileType describes the type of a given file.
type FileType int

// Ext2 file types.
const (
	Unknown         FileType = 0
	RegularFile     FileType = C.EXT2_FT_REG_FILE
	Directory       FileType = C.EXT2_FT_DIR
	CharacterDevice FileType = C.EXT2_FT_CHRDEV
	BlockDevice     FileType = C.EXT2_FT_BLKDEV
	Fifo            FileType = C.EXT2_FT_FIFO
	Socket          FileType = C.EXT2_FT_SOCK
	Symlink         FileType = C.EXT2_FT_SYMLINK
)

// Dirent represents a directory entry in an ext2 file system.
type Dirent struct {
	// The name of the directory entry.
	Name string

	// The type of the directory entry.
	Type FileType
}

func modeToFileType(mode C.__u16) C.int {
	switch mode & C.LINUX_S_IFMT {
	case C.LINUX_S_IFSOCK:
		return C.EXT2_FT_SOCK
	case C.LINUX_S_IFLNK:
		return C.EXT2_FT_SYMLINK
	case C.LINUX_S_IFREG:
		return C.EXT2_FT_REG_FILE
	case C.LINUX_S_IFBLK:
		return C.EXT2_FT_BLKDEV
	case C.LINUX_S_IFDIR:
		return C.EXT2_FT_DIR
	case C.LINUX_S_IFCHR:
		return C.EXT2_FT_CHRDEV
	case C.LINUX_S_IFIFO:
		return C.EXT2_FT_FIFO
	default:
		return 0
	}
}

// OpenFlags define the options a client can request when opening a file or directory.
type OpenFlags int

const (
	// Create indicates that the requested file or directory should be created if it doesn't
	// already exist.
	Create OpenFlags = 1 << iota

	// Exclusive indicates that the requested file or directory must not already exist.  It
	// is only meaningful when used with Create.
	Exclusive
)

// Dir represents a directory on the file system.
type Dir struct {
	inode
}

// Read returns the contents of the directory and an error, if any.
func (d *Dir) Read() ([]Dirent, error) {
	var entries []Dirent
	err := forEachDirent(d.fs, d.ino, 0, func(ent *C.struct_ext2_dir_entry) C.int {
		entries = append(entries, Dirent{
			Name: C.GoStringN(&ent.name[0], (C.int)(ent.name_len&0xff)),
			Type: FileType(ent.name_len >> 8),
		})
		return 0
	})

	return entries, errors.Wrapf(err, "ino=%v", d.ino)
}

// dirIterCB is the counter-part to the extern C function with the same name declared at the
// top of this file.  We use the export keyword to make it callable from C code.
//export dirIterCB
func dirIterCB(dirent *C.struct_ext2_dir_entry, _ C.int, _ C.int, _ *C.char, priv unsafe.Pointer) C.int {
	fn := cpointer.MustValue(uintptr(priv)).(func(*C.struct_ext2_dir_entry) C.int)

	return fn(dirent)
}

// forEachDirent iterates over every entry that the directory holds.  |flags| should
// be a bitwise OR of the dirent iteration flags declared in libext2fs.  |do| will be called once
// for every directory entry in the directory and must return 0 to continue iteration or return
// C.DIRENT_ABORT to stop iteration.  |do| must also return C.DIRENT_CHANGED if it changes
// any of the directory entries.  Returns an error if the inode is not a directory.
func forEachDirent(fs C.ext2_filsys, ino C.ext2_ino_t, flags C.int, do func(*C.struct_ext2_dir_entry) C.int) error {
	priv := cpointer.New(do)
	defer cpointer.MustDelete(priv)

	// This will make its way back to dirIterCB above.
	if err := check(C.dir_iterate(fs, ino, C.int(flags), unsafe.Pointer(priv))); err != nil {
		return errors.Wrap(err, "failed to iterate over directory")
	}

	return nil
}

// lookup checks d to see if it contains an entry named name.  Returns the inode
// number of the entry if one is found or an error.  lookup will perform a simple string
// comparison between name and the entries in the directory.  Callers that wish to perform
// directory traversal should use Namei.
func (d *Dir) lookup(name string) (C.ext2_ino_t, error) {
	n := C.CString(name)
	defer C.free(unsafe.Pointer(n))

	var ino C.ext2_ino_t
	errcode := C.ext2fs_lookup(d.fs, d.ino, n, C.int(C.strlen(n)), nil, &ino)
	if errcode == C.EXT2_ET_FILE_NOT_FOUND {
		return 0, errors.Wrap(ErrNotFound, name)
	}

	if err := check(errcode); err != nil {
		return 0, errors.Wrap(err, "unexpected error during lookup")
	}

	return ino, nil
}

// namei traverses path and returns the inode number of the destination, if one exists.  d is
// considered both the root and the current directory for path.
func (d *Dir) namei(path string) (C.ext2_ino_t, error) {
	p := C.CString(path)
	defer C.free(unsafe.Pointer(p))

	var ino C.ext2_ino_t
	errcode := C.ext2fs_namei(d.fs, d.ino, d.ino, p, &ino)
	if errcode == C.EXT2_ET_FILE_NOT_FOUND {
		return 0, errors.Wrap(ErrNotFound, path)
	}
	if err := check(errcode); err != nil {
		return 0, errors.Wrap(err, fmt.Sprint("unable to resolve path ", path))
	}

	return ino, nil
}

// OpenFile opens the file pointed to by name.  name is first cleaned with path.Clean().  d is
// considered the root directory as well as the current directory for the cleaned path.  OpenFile
// will return an error if name does not exist unless the Create flag is provided.  If both the
// Create and Exclusive flags are provided then OpenFile will return an error if the requested
// file already exists.  OpenFile will return the requested file and an error, if any.  Callers
// must close the returned file to ensure that any changes made to the file are persisted to the disk.
// For example, if a file is unlinked from its parent directory while there is still an active
// reference to it, the underlying inode and blocks for that file will not be freed until the last
// active reference has been closed.
func (d *Dir) OpenFile(name string, flags OpenFlags) (*File, error) {
	if glog.V(2) {
		glog.Info("OpenFile: path=", name)
	}

	create := flags&Create != 0
	exclusive := flags&Exclusive != 0

	// Check if there already exists an entry with the requested name.
	n := path.Clean(name)
	ino, err := d.namei(n)
	if err == nil && create && exclusive {
		return nil, errors.Wrap(ErrAlreadyExists, name)
	}
	if errors.Cause(err) == ErrNotFound && !create {
		return nil, err
	}
	if err == nil {
		// Make sure the inode really is a file.
		var node C.struct_ext2_inode
		if err := check(C.ext2fs_read_inode(d.fs, ino, &node)); err != nil {
			return nil, errors.Wrap(err, "unable to read inode")
		}
		if modeToFileType(node.i_mode) != C.EXT2_FT_REG_FILE {
			return nil, errors.Wrap(ErrNotAFile, name)
		}
		if glog.V(1) {
			glog.Infof("OpenFile: existing file with ino=%v, name=%s\n", ino, path.Base(n))
		}
		return &File{newInode(d.fs, ino)}, nil
	}
	if errors.Cause(err) != ErrNotFound {
		return nil, errors.Wrap(err, "unable to resolve name")
	}

	// Get the parent directory.
	parent, err := d.namei(path.Dir(n))
	if err != nil {
		return nil, errors.Wrap(err, "unable to resolve parent directory")
	}

	// Allocate a new inode.
	var child C.ext2_ino_t
	mode := C.int(C.LINUX_S_IFREG | 0644)
	if err := check(C.ext2fs_new_inode(d.fs, parent, mode, nil, &child)); err != nil {
		return nil, errors.Wrap(err, "unable to allocate inode")
	}

	if glog.V(1) {
		glog.Infof("OpenFile: creating file with ino=%v/name=%s in dir=%v\n", child, path.Base(n), parent)
	}

	childName := C.CString(path.Base(n))
	defer C.free(unsafe.Pointer(childName))

	// Link the inode to its parent directory.
	errcode := C.ext2fs_link(d.fs, parent, childName, child, C.EXT2_FT_REG_FILE)
	if errcode == C.EXT2_ET_DIR_NO_SPACE {
		// Expand the parent directory if necessary.
		if err := check(C.ext2fs_expand_dir(d.fs, parent)); err != nil {
			return nil, errors.Wrap(err, "unable to expand directory size")
		}

		errcode = C.ext2fs_link(d.fs, parent, childName, child, C.EXT2_FT_REG_FILE)
	}

	if err := check(errcode); err != nil {
		return nil, errors.Wrap(err, "unable to link inode")
	}

	// Update the parent's modification time.
	if err := updateMtime(d.fs, d.ino); err != nil {
		return nil, errors.Wrap(err, "unable to update mtime")
	}

	// Start filling in the inode for the child.
	var node C.struct_ext2_inode
	node.i_mode = C.__u16(mode)
	node.i_links_count = 1

	// For now all new files are owned by root.
	node.i_uid = 0
	node.i_gid = 0

	// Write out the new inode.
	if err := check(C.ext2fs_write_new_inode(d.fs, child, &node)); err != nil {
		return nil, errors.Wrap(err, "unable to write new inode")
	}

	// Mark the inode as being in use.
	C.ext2fs_inode_alloc_stats(d.fs, child, 1 /* inUse */)

	// Aaaand we're done.
	return &File{newInode(d.fs, child)}, nil
}

// OpenDirectory opens the directory pointed to by name.  name is first cleaned with path.Clean().  d is
// considered the root directory as well as the current directory for the cleaned path.  OpenDirectory
// will return an error if name does not exist unless the Create flag is provided.  If both the
// Create and Exclusive flags are provided then OpenDirectory will return an error if the requested
// directory already exists.  OpenDirectory will return the requested directory and an error, if any.
// Callers must close the returned directory to ensure that changes made to the directory will persist
// to the disk.
func (d *Dir) OpenDirectory(name string, flags OpenFlags) (*Dir, error) {
	if glog.V(2) {
		glog.Info("OpenDirectory: path=", name)
	}

	create := flags&Create != 0
	exclusive := flags&Exclusive != 0

	// Check if there already exists an entry with the requested name.
	n := path.Clean(name)
	ino, err := d.namei(n)
	if err == nil && create && exclusive {
		return nil, errors.Wrap(ErrAlreadyExists, name)
	}
	if errors.Cause(err) == ErrNotFound && !create {
		return nil, err
	}
	if err == nil {
		// Make sure the inode really is a directory.
		var node C.struct_ext2_inode
		if err := check(C.ext2fs_read_inode(d.fs, ino, &node)); err != nil {
			return nil, errors.Wrap(err, "unable to read inode")
		}
		if modeToFileType(node.i_mode) != C.EXT2_FT_DIR {
			return nil, errors.Wrap(ErrNotADir, name)
		}
		if glog.V(1) {
			glog.Infof("OpenDirectory: existing directory with ino=%v, name=%s\n", ino, path.Base(n))
		}
		return &Dir{newInode(d.fs, ino)}, nil
	}
	if errors.Cause(err) != ErrNotFound {
		return nil, errors.Wrap(err, "unable to resolve name")
	}

	// Create the directory.
	parent, err := d.namei(path.Dir(n))
	if err != nil {
		return nil, errors.Wrap(err, "unable to resolve parent directory")
	}

	childName := C.CString(path.Base(n))
	defer C.free(unsafe.Pointer(childName))

	errcode := C.ext2fs_mkdir(d.fs, parent, 0, childName)
	if errcode == C.EXT2_ET_DIR_NO_SPACE {
		// Expand the parent directory.
		if err := check(C.ext2fs_expand_dir(d.fs, parent)); err != nil {
			return nil, errors.Wrap(err, "unable to expand directory size")
		}

		errcode = C.ext2fs_mkdir(d.fs, parent, 0, childName)
	}
	if err := check(errcode); err != nil {
		return nil, errors.Wrap(err, "unable to create directory")
	}

	// Figure out what inode number was allocated to the child.
	child, err := d.namei(n)
	if err != nil {
		errors.Wrap(err, "unable to lookup newly created directory")
	}

	if glog.V(1) {
		glog.Infof("OpenDirectory: created directory with ino=%v/name=%s in dir=%v\n", child, path.Base(n), parent)
	}
	return &Dir{newInode(d.fs, child)}, nil
}

// Rename renames the resource pointed to by src to dst.  Both src and dst are first cleaned
// with path.Clean().  d is considered both the root and current working directory for the cleaned
// paths.  Renaming a file or directory will not affect any active references to that file/directory.
// Rename will not overwrite dst if it already exists.  Returns an error, if any.
func (d *Dir) Rename(src, dst string) error {
	if glog.V(2) {
		glog.Infof("Rename: src=%s, dst=%s\n", src, dst)
	}

	from, to := path.Clean(src), path.Clean(dst)

	ino, err := d.namei(from)
	if err != nil || ino == 0 {
		return errors.Wrap(err, "unable to find source")
	}

	_, err = d.namei(to)
	if err == nil {
		return errors.Wrap(ErrAlreadyExists, dst)
	}
	if errors.Cause(err) != ErrNotFound {
		return errors.Wrap(err, "unable to lookup destination")
	}

	fparent, err := d.namei(path.Dir(from))
	if err != nil {
		return errors.Wrap(err, "unable to find source parent directory")
	}

	tparent, err := d.namei(path.Dir(to))
	if err != nil {
		return errors.Wrap(err, "unable to find destination parent directory")
	}

	if glog.V(1) {
		glog.Infof("Rename: moving ino=%v in dir=%v with name=%s to dir=%v with name=%s\n",
			ino, fparent, path.Base(from), tparent, path.Base(to))
	}

	var node C.struct_ext2_inode
	if err := check(C.ext2fs_read_inode(d.fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to read source inode")
	}

	basename := C.CString(path.Base(to))
	defer C.free(unsafe.Pointer(basename))
	fType := modeToFileType(node.i_mode)
	errcode := C.ext2fs_link(d.fs, tparent, basename, ino, fType)
	if errcode == C.EXT2_ET_DIR_NO_SPACE {
		// Expand the parent directory.
		if err := check(C.ext2fs_expand_dir(d.fs, tparent)); err != nil {
			return errors.Wrap(err, "unable to expand target directory size")
		}

		errcode = C.ext2fs_link(d.fs, tparent, basename, ino, fType)
	}
	if err := check(errcode); err != nil {
		return errors.Wrap(err, "unable to link source to target directory")
	}

	if fType == C.EXT2_FT_DIR {
		// Update the ".." entry.
		err := forEachDirent(d.fs, ino, 0, func(entry *C.struct_ext2_dir_entry) C.int {
			name := C.GoStringN(&entry.name[0], (C.int)(entry.name_len&0xff))
			if name == ".." {
				entry.inode = C.__u32(tparent)
				return C.DIRENT_ABORT | C.DIRENT_CHANGED
			}
			return 0
		})
		if err != nil {
			return errors.Wrapf(err, "ino=%v", ino)
		}

		// Move the link count from the old parent to the new parent.
		if err := check(C.ext2fs_read_inode(d.fs, fparent, &node)); err != nil {
			return errors.Wrap(err, "unable to read source parent inode")
		}
		node.i_links_count--
		if err := check(C.ext2fs_write_inode(d.fs, fparent, &node)); err != nil {
			return errors.Wrap(err, "unable to write source parent inode")
		}

		if err := check(C.ext2fs_read_inode(d.fs, tparent, &node)); err != nil {
			return errors.Wrap(err, "unable to read target parent inode")
		}
		node.i_links_count++
		if err := check(C.ext2fs_write_inode(d.fs, tparent, &node)); err != nil {
			return errors.Wrap(err, "unable to write target parent inode")
		}
	}

	// Remove the old link.
	oldbasename := C.CString(path.Base(from))
	defer C.free(unsafe.Pointer(oldbasename))
	if err := check(C.ext2fs_unlink(d.fs, fparent, oldbasename, ino, 0)); err != nil {
		return errors.Wrap(err, "unable to unlink source")
	}

	// Update timestamps.
	if err := updateCtime(d.fs, ino); err != nil {
		return errors.Wrap(err, "unable to update ctime for target")
	}
	if err := updateMtime(d.fs, tparent); err != nil {
		return errors.Wrap(err, "unable to update mtime for new directory")
	}
	if err := updateMtime(d.fs, fparent); err != nil {
		return errors.Wrap(err, "unable to update mtime for old directory")
	}

	// Ugh.  Flush the whole mess to make sure everything is consistent.
	return d.Flush()
}

// Flush does not return until all changes to this directory and its children have been
// persisted to stable storage.  Returns an error, if any.
func (d *Dir) Flush() error {
	if glog.V(2) {
		glog.Info("Flush: dir=", d.ino)
	}

	if err := check(C.ext2fs_flush(d.fs)); err != nil {
		return errors.Wrap(err, "unable to flush file system")
	}

	return nil
}

// Unlink unlinks target from its parent directory.  target is first cleaned with path.Clean().
// d is considered both the root directory and the current working directory for the cleaned
// path.  If target points to a directory, then the directory must be empty and it must not have
// any active references.  If there are no more directories that link to target, then the blocks
// held by target will be freed.  However, if target is a file and there are currently active
// references to it then the blocks will not be freed until all the active references have been
// closed.  Returns an error, if any.
func (d *Dir) Unlink(target string) error {
	if glog.V(2) {
		glog.Info("Unlink: target=", target)
	}

	tgt := path.Clean(target)
	ino, err := d.namei(tgt)
	if err != nil {
		return errors.Wrap(err, "unable to resolve target path")
	}

	var node C.struct_ext2_inode
	if err := check(C.ext2fs_read_inode(d.fs, ino, &node)); err != nil {
		return errors.Wrap(err, "unable to read target inode")
	}

	var isDir bool
	if modeToFileType(node.i_mode) == C.EXT2_FT_DIR {
		isDir = true
		empty := true
		err := forEachDirent(d.fs, ino, 0, func(entry *C.struct_ext2_dir_entry) C.int {
			name := C.GoStringN(&entry.name[0], (C.int)(entry.name_len&0xff))
			if name == "." || name == ".." {
				return 0
			}

			empty = false
			return C.DIRENT_ABORT
		})
		if err != nil {
			return errors.Wrapf(err, "ino=%v", ino)
		}

		if !empty {
			return errors.Wrap(ErrNotEmpty, target)
		}

		if _, ok := inoRefCount[inode{d.fs, ino}]; ok {
			return errors.Wrap(ErrIsActive, target)
		}
	}

	parent, err := d.namei(path.Dir(tgt))
	if err != nil {
		return errors.Wrap(err, "unable to resolve parent directory")
	}

	if glog.V(1) {
		glog.Infof("Unlink: removing ino=%v/name=%s from dir=%v\n", ino, path.Base(tgt), parent)
	}

	basename := C.CString(path.Base(tgt))
	defer C.free(unsafe.Pointer(basename))

	if err := check(C.ext2fs_unlink(d.fs, parent, basename, ino, 0 /* flags */)); err != nil {
		return errors.Wrap(err, "unable to unlink target")
	}

	// Update the parent inode.
	var pnode C.struct_ext2_inode
	if err := check(C.ext2fs_read_inode(d.fs, parent, &pnode)); err != nil {
		return errors.Wrap(err, "unable to read target parent inode")
	}

	// Update timestamps.
	now := C.__u32(time.Now().Unix() & int64(^uint32(0)))

	pnode.i_ctime = now
	pnode.i_mtime = now

	// Update the inode being removed.
	node.i_ctime = now

	// Directories have 2 links, one from the "." entry and one from their parent.
	if isDir {
		node.i_links_count -= 2
		// This directory no longer links to its parent.
		if pnode.i_links_count > 1 {
			pnode.i_links_count--
		}
	} else {
		node.i_links_count--
	}

	// Now that the inode has been updated, we need to ensure that the updated inode
	// is written out to disk.
	writenode := func() error {
		if err := check(C.ext2fs_write_inode(d.fs, ino, &node)); err != nil {
			return errors.Wrap(err, "unable to write target inode")
		}
		return nil
	}

	if err := check(C.ext2fs_write_inode(d.fs, parent, &pnode)); err != nil {
		// Ignore errors while writing out the child inode.
		writenode()
		return errors.Wrap(err, "unable to write target parent inode")
	}

	if node.i_links_count <= 0 {
		node.i_dtime = now
	}

	if node.i_links_count > 0 || inoRefCount[inode{d.fs, ino}] > 0 {
		// There are still other links or active connections to this inode.  Just
		// write it out now.
		return writenode()
	}

	if err := freeBlocks(d.fs, ino, &node); err != nil {
		writenode()
		return errors.Wrap(err, "unable to de-allocate blocks for target inode")
	}

	return writenode()
}
