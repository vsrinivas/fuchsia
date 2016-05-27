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

package filesystem

import (
	"path"
	"strings"

	mojoerr "interfaces/errors"
	"interfaces/filesystem/common"
	"interfaces/filesystem/directory"
	mojofile "interfaces/filesystem/file"
	"mojo/public/go/bindings"
	"mojo/public/go/system"

	"fuchsia.googlesource.com/thinfs/lib/ext2fs"
	"github.com/golang/glog"
	"github.com/pkg/errors"
)

// dir holds a pointer to an inode and any connection-specific state.
type dir struct {
	fs    *filesystem
	dir   *ext2fs.Dir
	flags common.OpenFlags
}

func convertError(err error) (mojoerr.Error, error) {
	switch errors.Cause(err) {
	case ext2fs.ErrAlreadyExists:
		return mojoerr.Error_AlreadyExists, nil
	case ext2fs.ErrNotFound:
		return mojoerr.Error_NotFound, nil
	case ext2fs.ErrNotADir:
		return mojoerr.Error_FailedPrecondition, nil
	case ext2fs.ErrNotAFile:
		return mojoerr.Error_FailedPrecondition, nil
	case ext2fs.ErrNotEmpty:
		return mojoerr.Error_FailedPrecondition, nil
	case ext2fs.ErrIsActive:
		return mojoerr.Error_FailedPrecondition, nil
	default:
		return mojoerr.Error_Internal, err
	}
}

func serveDirectory(fs *filesystem, e2dir *ext2fs.Dir, req directory.Directory_Request, flags common.OpenFlags) {
	d := &dir{
		fs:    fs,
		dir:   e2dir,
		flags: flags,
	}
	stub := directory.NewDirectoryStub(req, d, bindings.GetAsyncWaiter())

	go func() {
		var err error
		for err == nil {
			err = stub.ServeRequest()
		}

		connErr, ok := err.(*bindings.ConnectionError)
		if !ok || !connErr.Closed() {
			// Log any error that's not a connection closed error.
			glog.Error(err)
		}

		d.fs.Lock()
		if err := d.dir.Close(); err != nil {
			glog.Error(err)
		}
		d.fs.Unlock()

		// The recount should have been incremented before serveDirectory was called.
		d.fs.decRef()
	}()
}

func convertFileType(ft ext2fs.FileType) directory.FileType {
	switch ft {
	case ext2fs.Directory:
		return directory.FileType_Directory
	case ext2fs.RegularFile:
		return directory.FileType_RegularFile
	default:
		return directory.FileType_Unknown
	}
}

func (d *dir) Read() (*[]directory.DirectoryEntry, mojoerr.Error, error) {
	if glog.V(2) {
		glog.Info("Read")
	}

	d.fs.Lock()
	entries, err := d.dir.Read()
	d.fs.Unlock()

	if err != nil {
		return nil, mojoerr.Error_Internal, err
	}

	out := make([]directory.DirectoryEntry, len(entries))
	for i, entry := range entries {
		out[i] = directory.DirectoryEntry{
			Type: convertFileType(entry.Type),
			Name: entry.Name,
		}
	}
	return &out, mojoerr.Error_Ok, nil
}

func (d *dir) ReadTo(src system.ProducerHandle) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Info("ReadTo: src=%\n", src)
	}

	d.fs.Lock()
	entries, err := d.dir.Read()
	d.fs.Unlock()

	if err != nil {
		return mojoerr.Error_Internal, err
	}

	go func() {
		for _, e := range entries {
			dirent := &directory.DirectoryEntry{
				Type: convertFileType(e.Type),
				Name: e.Name,
			}
			encoder := bindings.NewEncoder()
			if err := dirent.Encode(encoder); err != nil {
				glog.Errorf("Unable to encode directory entry with name=%s, type=%v: %v\n",
					dirent.Name, dirent.Type, err)
				return
			}

			data, _, err := encoder.Data()
			if err != nil {
				glog.Errorf("Unable to fetch encoded data for directory entry with name=%s, type=%v: %v\n",
					dirent.Name, dirent.Type, err)
				return
			}

			for len(data) > 0 {
				res, p := src.BeginWriteData(system.MOJO_WRITE_DATA_FLAG_NONE)
				if res != system.MOJO_RESULT_OK {
					glog.Error("Unable to begin 2-phase write: ", err)
					return
				}
				n := copy(p, data)
				res = src.EndWriteData(n)
				if res != system.MOJO_RESULT_OK {
					glog.Error("Unable to complete 2-phase write: ", err)
					return
				}
				data = data[n:]
			}
		}
	}()

	return mojoerr.Error_Ok, nil
}

func (d *dir) checkFlags(flags common.OpenFlags) (common.OpenFlags, mojoerr.Error) {
	ro := flags&common.OpenFlags_ReadOnly != 0
	rw := flags&common.OpenFlags_ReadWrite != 0

	if ro && rw {
		return 0, mojoerr.Error_InvalidArgument
	}

	var outFlags common.OpenFlags
	// The only flags we care about are read-only and read-write.
	if ro {
		// Dropping to read-only is always allowed.
		outFlags = common.OpenFlags_ReadOnly
	} else if rw {
		if d.flags&common.OpenFlags_ReadWrite == 0 {
			// Cannot request read-write permission if the client doesn't already have it.
			return 0, mojoerr.Error_PermissionDenied
		}

		outFlags = common.OpenFlags_ReadWrite
	} else {
		// Either the read-only or the read-write flag must be provided.
		return 0, mojoerr.Error_InvalidArgument
	}

	return outFlags, mojoerr.Error_Ok
}

func (d *dir) OpenFile(filepath string, req mojofile.File_Request, flags common.OpenFlags) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("OpenFile: filepath=%s, req=%v, flags=%v\n", filepath, req, flags)
	}

	d.fs.Lock()
	defer d.fs.Unlock()

	cleanpath := path.Clean(filepath)
	if cleanpath == "." || strings.HasPrefix(cleanpath, "../") {
		return mojoerr.Error_InvalidArgument, nil
	}

	connFlags, flagerr := d.checkFlags(flags)
	if flagerr != mojoerr.Error_Ok {
		return flagerr, nil
	}

	var oflags ext2fs.OpenFlags
	if flags&common.OpenFlags_Create != 0 {
		oflags |= ext2fs.Create
	}
	if flags&common.OpenFlags_Exclusive != 0 {
		oflags |= ext2fs.Exclusive
	}

	newfile, err := d.dir.OpenFile(cleanpath, oflags)
	if err != nil {
		return convertError(err)
	}

	// Increment the refcount now to avoid a race condition where the refcount goes to
	// zero before the new handle has a chance to increment it.
	d.fs.refcnt++
	serveFile(d.fs, newfile, req, connFlags)
	return mojoerr.Error_Ok, nil
}

func (d *dir) OpenDirectory(dirpath string, req directory.Directory_Request, flags common.OpenFlags) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("OpenDirectory: dirpath=%s, req=%v, flags=%v\n", dirpath, req, flags)
	}

	d.fs.Lock()
	defer d.fs.Unlock()

	cleanPath := path.Clean(dirpath)
	if cleanPath == "." || strings.HasPrefix(cleanPath, "../") {
		return mojoerr.Error_InvalidArgument, nil
	}

	connFlags, flagerr := d.checkFlags(flags)
	if flagerr != mojoerr.Error_Ok {
		return flagerr, nil
	}

	var oflags ext2fs.OpenFlags
	if flags&common.OpenFlags_Create != 0 {
		oflags |= ext2fs.Create
	}
	if flags&common.OpenFlags_Exclusive != 0 {
		oflags |= ext2fs.Exclusive
	}

	newdir, err := d.dir.OpenDirectory(cleanPath, oflags)
	if err != nil {
		return convertError(err)
	}

	// Increment the refcount now to avoid a race condition where the refcount goes to
	// zero before the new handle has a chance to increment it.
	d.fs.refcnt++
	serveDirectory(d.fs, newdir, req, connFlags)
	return mojoerr.Error_Ok, nil
}

func (d *dir) Rename(from string, to string) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("Rename: from=%s, to=%s\n", from, to)
	}

	d.fs.Lock()
	defer d.fs.Unlock()

	if d.flags&common.OpenFlags_ReadWrite != 0 {
		return mojoerr.Error_PermissionDenied, nil
	}

	if err := d.dir.Rename(path.Clean(from), path.Clean(to)); err != nil {
		return convertError(err)
	}
	return mojoerr.Error_Ok, nil
}

func (d *dir) Barrier(_ *[]string) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Info("Barrier")
	}

	d.fs.Lock()
	defer d.fs.Unlock()

	if d.flags&common.OpenFlags_ReadWrite != 0 {
		return mojoerr.Error_PermissionDenied, nil
	}

	if err := d.dir.Flush(); err != nil {
		return convertError(err)
	}
	return mojoerr.Error_Ok, nil
}

func (d *dir) Delete(name string) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("Delete: path=%s\n", name)
	}

	d.fs.Lock()
	defer d.fs.Unlock()

	if d.flags&common.OpenFlags_ReadWrite != 0 {
		return mojoerr.Error_PermissionDenied, nil
	}

	if err := d.dir.Unlink(path.Clean(name)); err != nil {
		return convertError(err)
	}
	return mojoerr.Error_Ok, nil
}

func (d *dir) Clone(req directory.Directory_Request) (mojoerr.Error, error) {
	return d.Reopen(req, d.flags)
}

func (d *dir) Reopen(req directory.Directory_Request, flags common.OpenFlags) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("Reopen: req=%v, flags=%v\n", req, flags)
	}

	d.fs.Lock()
	defer d.fs.Unlock()

	connFlags, flagerr := d.checkFlags(flags)
	if flagerr != mojoerr.Error_Ok {
		return flagerr, nil
	}

	newdir, err := d.dir.OpenDirectory(".", 0)
	if err != nil {
		return convertError(err)
	}

	// Increment the refcount now to avoid a race condition where the refcount goes to
	// zero before the new handle has a chance to increment it.
	d.fs.refcnt++
	serveDirectory(d.fs, newdir, req, connFlags)
	return mojoerr.Error_Ok, nil
}
