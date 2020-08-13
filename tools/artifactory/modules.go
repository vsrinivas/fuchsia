// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// BuildAPIModuleUploads returns a set of Uploads corresponding to the
// collection of build API modules in the build.
func BuildAPIModuleUploads(mods *build.Modules, namespace string) []Upload {
	return buildAPIModuleUploads(mods, namespace)
}

func buildAPIModuleUploads(mods apiModules, namespace string) []Upload {
	var uploads []Upload
	for _, api := range mods.APIs() {
		apiFilename := api + ".json"
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), apiFilename),
			Destination: path.Join(namespace, apiFilename),
		})
	}
	return uploads
}

type apiModules interface {
	BuildDir() string
	APIs() []string
}
