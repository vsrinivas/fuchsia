// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

func allFidlFiles() []FidlFile {
	return []FidlFile{
		ByteArrayFidl,
	}
}

func allGidlFiles() []GidlFile {
	return []GidlFile{
		ByteArrayGidl,
		GpuMagmaGidl,
		HardwareDisplayGidl,
	}
}
