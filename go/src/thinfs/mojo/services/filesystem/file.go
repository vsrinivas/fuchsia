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
	mojoerr "interfaces/errors"
	"interfaces/filesystem/common"
	mojofile "interfaces/filesystem/file"
	"io"
	"mojo/public/go/bindings"
	"mojo/public/go/system"

	"fuchsia.googlesource.com/thinfs/lib/ext2fs"
	"github.com/golang/glog"
	"github.com/pkg/errors"
)

type file struct {
	fs    *filesystem
	file  *ext2fs.File
	flags common.OpenFlags
}

func serveFile(fs *filesystem, e2file *ext2fs.File, req mojofile.File_Request, flags common.OpenFlags) {
	f := &file{
		fs:    fs,
		file:  e2file,
		flags: flags,
	}
	stub := mojofile.NewFileStub(req, f, bindings.GetAsyncWaiter())

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

		f.fs.Lock()
		if err := f.file.Close(); err != nil {
			glog.Error(err)
		}
		f.fs.Unlock()

		// The recount should have been incremented before serveFile was called.
		f.fs.decRef()
	}()
}

func (f *file) readAt(p []byte, off int64) (int, error) {
	f.fs.Lock()
	n, err := f.file.ReadAt(p, off)
	f.fs.Unlock()

	return n, err
}

func (f *file) ReadAt(off int64, size int64) (*[]uint8, mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("ReadAt: off=%v, size=%v\n", off, size)
	}

	if off < 0 || size < 0 {
		return nil, mojoerr.Error_InvalidArgument, nil
	}

	fsize, err := f.size()
	if err != nil {
		return nil, mojoerr.Error_Internal, errors.Wrap(err, "unable to get file size")
	}
	if fsize-off < 0 {
		return nil, mojoerr.Error_InvalidArgument, nil
	}

	var bufsize int64
	if size < fsize-off {
		bufsize = size
	} else {
		bufsize = fsize - off
	}

	p := make([]uint8, bufsize)
	n, err := f.readAt(p, off)
	if err != nil && err != io.EOF {
		return nil, mojoerr.Error_Internal, errors.Wrap(err, "unable to read file")
	}

	outerr := mojoerr.Error_Ok
	if int64(n) < size {
		outerr = mojoerr.Error_OutOfRange
	}

	return &p, outerr, nil
}

func (f *file) writeAt(p []byte, off int64) (int, error) {
	f.fs.Lock()
	n, err := f.file.WriteAt(p, off)
	f.fs.Unlock()

	return n, err
}

func (f *file) WriteAt(p []uint8, off int64) (int64, mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("ReadAt: off=%v, len(p)=%v\n", off, len(p))
	}

	if f.flags&common.OpenFlags_ReadWrite != 0 {
		return 0, mojoerr.Error_PermissionDenied, nil
	}
	if off < 0 {
		return 0, mojoerr.Error_InvalidArgument, nil
	}

	n, err := f.writeAt(p, off)
	if err != nil {
		return int64(n), mojoerr.Error_Internal, errors.Wrap(err, "unable to write file")
	}

	return int64(n), mojoerr.Error_Ok, nil
}

func (f *file) ReadTo(src system.ProducerHandle, off int64, size int64) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("ReadTo: src=%v, off=%v, size=%v\n", src, off, size)
	}

	if off < 0 || size < 0 {
		return mojoerr.Error_InvalidArgument, nil
	}

	fsize, err := f.size()
	if err != nil {
		return mojoerr.Error_Internal, errors.Wrap(err, "unable to get file size")
	}
	if fsize-off < 0 {
		return mojoerr.Error_InvalidArgument, nil
	}

	var rem int64
	if size < fsize-off {
		rem = size
	} else {
		rem = fsize - off
	}

	go func() {
		for rem > 0 {
			res, p := src.BeginWriteData(system.MOJO_WRITE_DATA_FLAG_NONE)
			if res != system.MOJO_RESULT_OK {
				break
			}

			n, err := f.readAt(p, off)
			res = src.EndWriteData(n)

			if err != nil || res != system.MOJO_RESULT_OK {
				break
			}
			off += int64(n)
			rem -= int64(n)
		}
	}()
	return mojoerr.Error_Ok, nil
}

func (f *file) WriteFrom(sink system.ConsumerHandle, off int64) (mojoerr.Error, error) {
	if glog.V(2) {
		glog.Infof("WriteFrom: sink=%v, off=%v\n", sink, off)
	}

	if off < 0 {
		return mojoerr.Error_InvalidArgument, nil
	}

	go func() {
		for {
			res, p := sink.BeginReadData(system.MOJO_READ_DATA_FLAG_NONE)
			if res != system.MOJO_RESULT_OK {
				break
			}

			n, err := f.writeAt(p, off)
			res = sink.EndReadData(n)

			if err != nil || res != system.MOJO_RESULT_OK {
				break
			}
			off += int64(n)
		}
	}()
	return mojoerr.Error_Ok, nil
}

func (f *file) size() (int64, error) {
	f.fs.Lock()
	n, err := f.file.Size()
	f.fs.Unlock()

	return n, err
}

func (f *file) Size() (int64, mojoerr.Error, error) {
	if glog.V(2) {
		glog.Info("Size")
	}

	fsize, err := f.size()
	if err != nil {
		return 0, mojoerr.Error_Internal, errors.Wrap(err, "unable to fetch file size")
	}

	return fsize, mojoerr.Error_Ok, nil
}

func (*file) Clone(inFile mojofile.File_Request) (outErr mojoerr.Error, err error) {
	return mojoerr.Error_Unimplemented, nil
}

func (*file) Reopen(inFile mojofile.File_Request, inFlags common.OpenFlags) (outErr mojoerr.Error, err error) {
	return mojoerr.Error_Unimplemented, nil
}
