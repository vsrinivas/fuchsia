// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// Tool represents a host tool in the build.
type Tool struct {
	// Name is the canonical name of the image.
	Name string `json:"name"`

	// Path is relative path to the tool within the build directory.
	Path string `json:"path"`

	// OS is the operating system the tool is meant to run on.
	OS string

	// CPU is the architecture the tool is meant to run on.
	CPU string
}
