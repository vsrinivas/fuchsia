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
