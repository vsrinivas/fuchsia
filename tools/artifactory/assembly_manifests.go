// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// AssemblyManifestsUploads generates the Uploads for each assembly manifest produced by the build.
func AssemblyManifestsUploads(mods *build.Modules, namespace string) []Upload {
	return assemblyManifestsUploads(mods, namespace)
}

func assemblyManifestsUploads(mods assemblyManifestsModules, namespace string) []Upload {
	var uploads []Upload
	for _, manifest := range mods.AssemblyManifests() {
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), manifest.AssemblyManifestPath),
			Destination: path.Join(namespace, fmt.Sprintf("%s.json", manifest.ImageName)),
		})
	}
	return uploads
}

type assemblyManifestsModules interface {
	BuildDir() string
	AssemblyManifests() []build.AssemblyManifest
}
