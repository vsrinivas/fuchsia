// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

var (
	toolsToUpload = map[string]string{
		"zbi":                   "zbi",
		"fvm":                   "fvm",
		"mtd-redundant-storage": "mtd-redundant-storage",
		// TODO(fxbug.dev/38517): We can remove this different destination
		// name once the go bootserver has replaced the old c bootserver
		// and is called bootserver instead of bootserver_new.
		"bootserver_new": "bootserver",
		"device-finder":  "device-finder",
	}
)

// ToolUploads returns a set of tools to upload.
func ToolUploads(mods *build.Modules, namespace string) []Upload {
	return toolUploads(mods, toolsToUpload, namespace)
}

func toolUploads(mods toolModules, whitelist map[string]string, namespace string) []Upload {
	var uploads []Upload
	for _, tool := range mods.Tools() {
		if _, ok := whitelist[tool.Name]; !ok {
			continue
		}
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), tool.Path),
			Destination: path.Join(namespace, fmt.Sprintf("%s-%s", tool.OS, tool.CPU), whitelist[tool.Name]),
		})
	}
	return uploads
}

type toolModules interface {
	BuildDir() string
	Tools() []build.Tool
}
