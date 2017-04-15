// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build darwin

package gpt

import "os"

const (
	// IOCTLPhysical = DKIOCGETPHYSICALBLOCKSIZE is the darwin ioctl flag for physical block size
	IOCTLPhysical = 1074029645

	// IOCTLLogical = DKIOCGETBLOCKSIZE is the darwin ioctl flag for logical block size
	IOCTLLogical = 1074029592

	// IOCTLSize = DKIOCGETBLOCKCOUNT is the darwin ioctl flag for disk block count
	IOCTLSize = 1074291737
)

// GetOptimalTransferSize returns the optimal transfer size of the given disk.
// Unimplemented on OSX, it just returns 0.
func GetOptimalTransferSize(f *os.File) (uint64, error) {
	return 0, nil
}
