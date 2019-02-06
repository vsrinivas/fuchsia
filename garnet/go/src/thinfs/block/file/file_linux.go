// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

// #include <linux/falloc.h>
// #include <linux/fs.h>
import "C"

import (
	"syscall"
	"unsafe"
)

func ioctlBlockGetSize(fd uintptr) (int64, error) {
	var size uint64

	if _, _, err := syscall.Syscall(syscall.SYS_IOCTL, fd, C.BLKGETSIZE64, uintptr(unsafe.Pointer(&size))); err != 0 {
		return 0, err
	}

	return int64(size), nil
}

func ioctlBlockDiscard(fd uintptr, off, len uint64) error {
	var r [2]uint64
	r[0] = off
	r[1] = len

	if _, _, err := syscall.Syscall(syscall.SYS_IOCTL, fd, C.BLKDISCARD, uintptr(unsafe.Pointer(&r[0]))); err != 0 {
		return err
	}

	return nil
}

func ioctlBlockGetSectorSize(fd uintptr) (int64, error) {
	var sectorSize C.int
	if _, _, err := syscall.Syscall(syscall.SYS_IOCTL, fd, C.BLKSSZGET, uintptr(unsafe.Pointer(&sectorSize))); err != 0 {
		return 0, err
	}

	return int64(sectorSize), nil
}

func fallocate(fd uintptr, off, len int64) error {
	return syscall.Fallocate(int(fd), C.FALLOC_FL_PUNCH_HOLE|C.FALLOC_FL_KEEP_SIZE, off, len)
}
