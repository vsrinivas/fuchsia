// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"fmt"

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

// LookupPath returns the path (relative to the build directory) of the named tool
// built for the specified platform. It will return an error if the
// platform/tool combination cannot be found, generally because the platform is
// not supported or because the tool is not listed in tool_paths.json.
func (t Tools) LookupPath(platform, name string) (string, error) {
	for _, tool := range t {
		toolPlatform := hostplatform.MakeName(tool.OS, tool.CPU)
		if name == tool.Name && platform == toolPlatform {
			return tool.Path, nil
		}
	}
	return "", fmt.Errorf("no tool with platform %q and name %q", platform, name)
}
