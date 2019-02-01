// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build linux

package gpt

import (
	"os"
	"unsafe"
)

const (
	// IOCTLPhysical = BLKBSZGET is the linux ioctl flag for physical block size
	IOCTLPhysical = 2148012656

	// IOCTLLogical = BLKSSZGET is the linux ioctl flag for logical block size
	IOCTLLogical = 4712

	// IOCTLOptimal = BLKIOOPT is the linux ioctl flag for optimal transfer size
	IOCTLOptimal = 4729

	// IOCTLSize = BLKGETSIZE is the linux ioctl flag for getting disk block count
	IOCTLSize = 4704
)

// GetOptimalTransferSize returns the optimal transfer size of the given disk.
func GetOptimalTransferSize(f *os.File) (uint64, error) {
	var sz uint64
	if err := ioctl(uintptr(f.Fd()), IOCTLOptimal, uintptr(unsafe.Pointer(&sz))); err != nil {
		return 0, err
	}
	return uint64(sz), nil
}
