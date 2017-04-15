// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build linux,darwin,unix !windows !fuchsia

package gpt

import (
	"os"
	"syscall"
	"unsafe"
)

func ioctl(fd, op, v uintptr) error {
	_, _, er := syscall.Syscall(syscall.SYS_IOCTL, fd, op, v)
	if er != 0 {
		return os.NewSyscallError("ioctl", er)
	}
	return nil
}

// GetPhysicalBlockSize fetches the physical block size of the given file. It
// requires elevated process priviliges to execute on most platforms. Currently
// only supported on Linux and Darwin.
func GetPhysicalBlockSize(f *os.File) (uint64, error) {
	if _, regular := regularSize(f); regular {
		return FallbackPhysicalBlockSize, nil
	}

	var sz uint32
	if err := ioctl(uintptr(f.Fd()), IOCTLPhysical, uintptr(unsafe.Pointer(&sz))); err != nil {
		return FallbackPhysicalBlockSize, err
	}
	return uint64(sz), nil
}

// GetLogicalBlockSize fetches the physical block size of the given file. It
// requires elevated process priviliges to execute on most platforms. Currently
// only supported on Linux and Darwin.
func GetLogicalBlockSize(f *os.File) (uint64, error) {
	if _, regular := regularSize(f); regular {
		return FallbackLogicalBlockSize, nil
	}

	var sz uint32
	if err := ioctl(uintptr(f.Fd()), IOCTLLogical, uintptr(unsafe.Pointer(&sz))); err != nil {
		return FallbackLogicalBlockSize, err
	}
	return uint64(sz), nil
}

// GetDiskSize fetches the byte size of the given disk.
func GetDiskSize(f *os.File) (uint64, error) {
	if sz, regular := regularSize(f); regular {
		return uint64(sz), nil
	}

	var sz uint64
	if err := ioctl(uintptr(f.Fd()), IOCTLSize, uintptr(unsafe.Pointer(&sz))); err != nil {
		return 0, err
	}

	lbs, err := GetLogicalBlockSize(f)
	if err != nil {
		return 0, err
	}

	return uint64(lbs) * sz, nil
}
