// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// ImageUploads parses the image manifest located in buildDir and returns a
// list of Uploads for the images used for testing.
func ImageUploads(mods *build.Modules, namespace string) []Upload {
	return imageUploads(mods, namespace)
}

func imageUploads(mods imgModules, namespace string) []Upload {
	manifestName := filepath.Base(mods.ImageManifest())

	files := []Upload{
		{
			Source:      mods.ImageManifest(),
			Destination: filepath.Join(namespace, manifestName),
		},
	}

	// The same image might appear in multiple entries.
	seen := make(map[string]bool)
	for _, img := range mods.Images() {
		if _, ok := seen[img.Path]; !ok && isActualImage(img) {
			files = append(files, Upload{
				Source:      filepath.Join(mods.BuildDir(), img.Path),
				Destination: filepath.Join(namespace, img.Path),
			})
			seen[img.Path] = true
		}
	}
	return files
}

type imgModules interface {
	BuildDir() string
	Images() []build.Image
	ImageManifest() string
}

func isActualImage(img build.Image) bool {
	return len(img.PaveArgs) > 0 || len(img.NetbootArgs) > 0 || len(img.PaveZedbootArgs) > 0 || img.Name == "qemu-kernel" || img.Name == "storage-full"
}
