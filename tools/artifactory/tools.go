// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

var (
	toolsToUpload = map[string]struct{}{
		"zbi":                   struct{}{},
		"fvm":                   struct{}{},
		"mtd-redundant-storage": struct{}{},
	}
)

// ToolUploads returns a set of tools to upload.
func ToolUploads(mods *build.Modules, namespace string) []Upload {
	return toolUploads(mods, toolsToUpload, namespace)
}

func toolUploads(mods toolModules, whitelist map[string]struct{}, namespace string) []Upload {
	var uploads []Upload
	for _, tool := range mods.Tools() {
		if _, ok := whitelist[tool.Name]; !ok {
			continue
		}
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), tool.Path),
			Destination: path.Join(namespace, fmt.Sprintf("%s-%s", tool.OS, tool.CPU), tool.Name),
		})
	}
	return uploads
}

type toolModules interface {
	BuildDir() string
	Tools() []build.Tool
}
