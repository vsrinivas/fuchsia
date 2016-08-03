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
	"runtime"
	"sync"

	"interfaces/block"
	mojoerr "interfaces/errors"
	"interfaces/filesystem/common"
	"interfaces/filesystem/directory"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	blk "fuchsia.googlesource.com/thinfs/mojo/clients/block"
	"github.com/golang/glog"
	"github.com/pkg/errors"
)

type filesystem struct {
	sync.Mutex
	refcnt int
	fs     fs.FileSystem
}

func (fs *filesystem) decRef() {
	fs.Lock()
	if fs.refcnt--; fs.refcnt <= 0 {
		if err := fs.fs.Close(); err != nil {
			glog.Error(err)
		}
		runtime.SetFinalizer(fs, nil)
	}
	fs.Unlock()
}

// Factory implements filesystem.FileSystem.
type Factory struct{}

// OpenFileSystem implements the OpenFileSystem method of the FileSystem mojo interface.
func (Factory) OpenFileSystem(ptr block.Device_Pointer, req directory.Directory_Request) (mojoerr.Error, error) {
	dev, err := blk.New(ptr)
	if err != nil {
		return mojoerr.Error_Internal, errors.Wrap(err, "unable to create mojo client for block device")
	}

	var (
		openFlags common.OpenFlags
		fsFlags   fs.FileSystemOptions
	)
	if caps := dev.GetCapabilities(); caps == block.Capabilities_ReadWrite {
		openFlags = common.OpenFlags_ReadWrite
		fsFlags = fs.ReadWrite
	} else if caps == block.Capabilities_ReadOnly {
		openFlags = common.OpenFlags_ReadOnly
		fsFlags = fs.ReadOnly
	} else {
		return mojoerr.Error_PermissionDenied, nil
	}

	// TODO(smklein): Use different constructors for different filesystems.
	vfs, err := msdosfs.New("ThinFS FAT", dev, fsFlags)
	if err != nil {
		return mojoerr.Error_FailedPrecondition, nil
	}

	fs := &filesystem{
		fs:     vfs,
		refcnt: 1, // refcnt starts at 1 because of the root directory.
	}
	runtime.SetFinalizer(fs, func(*filesystem) {
		glog.Error("Filesystem became unreachable before it was closed.")
		if err := fs.fs.Close(); err != nil {
			glog.Error(err)
		}
	})

	serveDirectory(fs, vfs.RootDirectory(), req, openFlags)
	return mojoerr.Error_Ok, nil
}
