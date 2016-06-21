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
	"fmt"
	"runtime"
	"sync"

	"interfaces/block"
	mojoerr "interfaces/errors"
	"interfaces/filesystem/common"
	"interfaces/filesystem/directory"

	"fuchsia.googlesource.com/thinfs/lib/cpointer"
	"fuchsia.googlesource.com/thinfs/lib/ext2fs"
	blk "fuchsia.googlesource.com/thinfs/mojo/clients/block"
	"github.com/golang/glog"
	"github.com/pkg/errors"
)

type filesystem struct {
	sync.Mutex
	refcnt int
	ext2   *ext2fs.FS
}

func (fs *filesystem) decRef() {
	fs.Lock()
	if fs.refcnt--; fs.refcnt <= 0 {
		if err := fs.ext2.Close(); err != nil {
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
		flags     common.OpenFlags
		ext2flags ext2fs.Options
	)
	if caps := dev.GetCapabilities(); caps == block.Capabilities_ReadWrite {
		flags = common.OpenFlags_ReadWrite
		ext2flags = ext2fs.ReadWrite
	} else if caps == block.Capabilities_ReadOnly {
		flags = common.OpenFlags_ReadOnly
		ext2flags = ext2fs.ReadOnly
	} else {
		return mojoerr.Error_PermissionDenied, nil
	}

	// Use the uintptr returned by the cpointer package to refer to the device.  The I/O
	// manager in the ext2fs package will convert it back into a uintptr and use it to
	// get the device.
	path := fmt.Sprintf("%#x", cpointer.New(dev))
	ext2, err := ext2fs.New(path, ext2flags)
	if err != nil {
		return mojoerr.Error_FailedPrecondition, nil
	}

	fs := &filesystem{
		ext2:   ext2,
		refcnt: 1, // refcnt starts at 1 because of the root directory.
	}
	runtime.SetFinalizer(fs, func(*filesystem) {
		glog.Error("Filesystem became unreachable before it was closed.")
		if err := fs.ext2.Close(); err != nil {
			glog.Error(err)
		}
	})

	serveDirectory(fs, ext2.RootDirectory(), req, flags)
	return mojoerr.Error_Ok, nil
}
