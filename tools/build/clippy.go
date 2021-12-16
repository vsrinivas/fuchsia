// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// ClippyTarget represents a Rust clippy target. Building such a target runs
// clippy on the underlying Rust code.
type ClippyTarget struct {
	// Output is the path to the clippy output file.
	Output string `json:"output"`

	// Sources is a list of paths to source files that compose this target,
	// relative to the build directory.
	Sources []string `json:"src"`
}
