// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package iou

import (
	"os"
	"syscall"
	"syscall/zx/fdio"
)

// OpenFrom opens a path from a give fdio.Directory, using standard Go flags and
// mode. See os.OpenFile for flags and mode.
func OpenFrom(parent *fdio.Directory, path string, flags int, mode uint32) (*os.File, error) {
	zflags := preprocessFlags(syscall.FdioFlagsToZxio(uint32(flags)), mode)

	f, err := parent.Open(path, zflags, mode)
	if err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(syscall.OpenFDIO(f)), path), nil
}

func preprocessFlags(flags uint32, mode uint32) uint32 {
	flagsIncompatibleWithDirectory := (flags&syscall.FsRightWritable != 0) || (flags&syscall.FsFlagCreate != 0)
	// Special allowance for Mkdir
	if (flags == syscall.FsFlagCreate|syscall.FsFlagExclusive|syscall.FsRightReadable|syscall.FsRightWritable) &&
		(mode&syscall.S_IFDIR != 0) {
		flagsIncompatibleWithDirectory = false
	}
	if (flags&syscall.FsFlagDirectory) == 0 && flagsIncompatibleWithDirectory {
		flags |= syscall.FsFlagNotDirectory
	}
	return flags
}
