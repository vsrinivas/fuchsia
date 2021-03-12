// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
)

// Tool represents a host tool in the build.
type Tool struct {
	// Name is the canonical name of the image.
	Name string `json:"name"`

	// Path is relative path to the tool within the build directory.
	Path string `json:"path"`

	// OS is the operating system the tool is meant to run on.
	OS string `json:"os"`

	// CPU is the architecture the tool is meant to run on.
	CPU string `json:"cpu"`
}

type Tools []Tool

// AsMap returns a mapping from tool name to tool for every tool supported on
// the specified platform.
func (t Tools) AsMap(platform string) map[string]Tool {
	m := make(map[string]Tool)
	for _, tool := range t {
		if hostplatform.MakeName(tool.OS, tool.CPU) == platform {
			m[tool.Name] = tool
		}
	}
	return m
}
