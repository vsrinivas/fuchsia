// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !linux

package file

import "syscall"

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
