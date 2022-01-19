// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// AssemblyInputArchiveUploads parses the assembly input archives manifest
// located in the build and returns a list of Uploads for all archives listed.
func AssemblyInputArchiveUploads(mods *build.Modules, namespace string) []Upload {
	return assemblyInputArchiveUploads(mods, namespace)
}

func assemblyInputArchiveUploads(mods assemblyInputArchiveModules, namespace string) []Upload {
	var uploads []Upload
	for _, archive := range mods.AssemblyInputArchives() {
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), archive.Path),
			Destination: path.Join(namespace, filepath.Base(archive.Path)),
		})
	}
	return uploads
}

type assemblyInputArchiveModules interface {
	BuildDir() string
	AssemblyInputArchives() []build.AssemblyInputArchive
}
