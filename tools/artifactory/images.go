// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// ImageUploads parses the image manifest located in the build and returns a
// list of Uploads for the images used for testing.
func ImageUploads(mods *build.Modules, namespace string) []Upload {
	return imageUploads(mods, namespace)
}

func imageUploads(mods imgModules, namespace string) []Upload {
	manifestName := filepath.Base(mods.ImageManifest())

	files := []Upload{
		{
			Source:      mods.ImageManifest(),
			Destination: path.Join(namespace, manifestName),
		},
	}

	// The same image might appear in multiple entries.
	seen := make(map[string]struct{})
	for _, img := range mods.Images() {
		if _, ok := seen[img.Path]; !ok {
			files = append(files, Upload{
				Source:      filepath.Join(mods.BuildDir(), img.Path),
				Destination: path.Join(namespace, img.Path),
				Compress:    true,
			})
			seen[img.Path] = struct{}{}
		}
	}
	return files
}

type imgModules interface {
	BuildDir() string
	Images() []build.Image
	ImageManifest() string
}
