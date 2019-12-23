// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"thinfs/fs"

	"syscall/zx/io"
)

type dirDirEnt string

func (d dirDirEnt) GetType() fs.FileType {
	return fs.FileTypeDirectory
}

func (d dirDirEnt) GetIno() uint64 {
	return io.InoUnknown
}

func (d dirDirEnt) GetName() string {
	return string(d)
}

type fileDirEnt string

func (d fileDirEnt) GetType() fs.FileType {
	return fs.FileTypeRegularFile
}

func (d fileDirEnt) GetIno() uint64 {
	return io.InoUnknown
}

func (d fileDirEnt) GetName() string {
	return string(d)
}
