// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package gpt

// GetOptimalTransferSize returns the optimal transfer size of the given disk.
// Unimplemented on Fuchsia, it just returns 0.
func GetOptimalTransferSize(f *os.File) (uint64, error) {
	return 0, nil
}
