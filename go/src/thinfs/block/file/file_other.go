// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !linux

package file

import (
	"syscall"
	"syscall/mx/mxio"
)

func ioctlBlockGetSize(fd uintptr) (int64, error) {
	return 0, syscall.EOPNOTSUPP
}

func ioctlBlockDiscard(fd uintptr, off, size uint64) error {
	return syscall.EOPNOTSUPP
}

func ioctlBlockGetSectorSize(fd uintptr) (int64, error) {
	return 0, syscall.EOPNOTSUPP
}

func fallocate(fd uintptr, off, len int64) error {
	return syscall.EOPNOTSUPP
}

func ioctlDeviceGetTopoPath(fd uintptr) string {
	m := syscall.MXIOForFD(int(fd))
	path := make([]byte, 1024)
	_, err := m.Ioctl(mxio.IoctlDeviceGetTopoPath, nil, path)

	if err == nil {
		return string(path)
	}

	return ""
}
