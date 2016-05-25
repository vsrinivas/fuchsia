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
	"io"
	"unsafe"

	"github.com/golang/glog"
	"github.com/pkg/errors"
)

// File represents a file on the file system.
type File struct {
	inode
}

// ReadAt implements io.ReaderAt for File.
func (f *File) ReadAt(p []byte, off int64) (int, error) {
	if glog.V(2) {
		glog.Infof("ReadAt: ino=%v, off=%v, len(p)=%v\n", f.ino, off, len(p))
	}

	var fp C.ext2_file_t

	if err := check(C.ext2fs_file_open(f.fs, f.ino, 0, &fp)); err != nil {
		return 0, errors.Wrap(err, "unable to open file")
	}
	cleanup := func(f *File, fp C.ext2_file_t) error {
		if err := check(C.ext2fs_file_close(fp)); err != nil {
			return errors.Wrap(err, "unable to close file")
		}

		if err := updateAtime(f.fs, f.ino); err != nil {
			return errors.Wrap(err, "unable to update atime")
		}

		return nil
	}

	if err := check(C.ext2fs_file_llseek(fp, C.__u64(off), C.SEEK_SET, nil)); err != nil {
		// Ignore cleanup errors.
		cleanup(f, fp)
		return 0, errors.Wrap(err, "unable to seek")
	}

	var n C.uint
	buf := unsafe.Pointer(&p[0])
	if err := check(C.ext2fs_file_read(fp, buf, C.uint(len(p)), &n)); err != nil {
		cleanup(f, fp)
		return int(n), errors.Wrap(err, "unable to read file")
	}

	if int(n) < len(p) {
		cleanup(f, fp)
		return int(n), io.EOF
	}

	return int(n), cleanup(f, fp)
}

// WriteAt implements io.WriterAt for File.
func (f *File) WriteAt(p []byte, off int64) (int, error) {
	if glog.V(2) {
		glog.Infof("WriteAt: ino=%v, off=%v, len(p)=%v\n", f.ino, off, len(p))
	}

	var fp C.ext2_file_t

	if err := check(C.ext2fs_file_open(f.fs, f.ino, C.EXT2_FILE_WRITE, &fp)); err != nil {
		return 0, errors.Wrap(err, "unable to open file")
	}
	cleanup := func(f *File, fp C.ext2_file_t) error {
		if err := check(C.ext2fs_file_close(fp)); err != nil {
			return errors.Wrap(err, "unable to close file")
		}

		if err := updateMtime(f.fs, f.ino); err != nil {
			return errors.Wrap(err, "unable to update mtime")
		}

		return nil
	}

	if err := check(C.ext2fs_file_llseek(fp, C.__u64(off), C.SEEK_SET, nil)); err != nil {
		// Ignore cleanup errors.
		cleanup(f, fp)
		return 0, errors.Wrap(err, "unable to seek")
	}

	var n C.uint
	buf := unsafe.Pointer(&p[0])
	if err := check(C.ext2fs_file_write(fp, buf, C.uint(len(p)), &n)); err != nil {
		cleanup(f, fp)
		return int(n), errors.Wrap(err, "unable to write file")
	}

	if err := check(C.ext2fs_file_flush(fp)); err != nil {
		cleanup(f, fp)
		return int(n), errors.Wrap(err, "unable to flush file")
	}

	return int(n), cleanup(f, fp)
}

// Size returns the size of the file in bytes and an error, if any.
func (f *File) Size() (int64, error) {
	if glog.V(2) {
		glog.Info("Size: ino=", f.ino)
	}

	var fp C.ext2_file_t

	if err := check(C.ext2fs_file_open(f.fs, f.ino, 0, &fp)); err != nil {
		return 0, errors.Wrap(err, "unable to open file")
	}

	size := C.ext2fs_file_get_size(fp)

	if err := check(C.ext2fs_file_close(fp)); err != nil {
		return 0, errors.Wrap(err, "unable to close file")
	}

	return int64(size), nil
}
