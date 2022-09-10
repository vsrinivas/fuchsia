// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type ProductSizeCheckerOutput struct {
	// Visualization is the relative path to the directory where size visualization is stored within the build directory.
	Visualization string `json:"visualization"`

	// SizeBreakdown is the relative path to the size breakdown text file within the build directory.
	SizeBreakdown string `json:"size_breakdown"`
}
