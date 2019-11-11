// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build/api"
)

// ImageUploads parses the image manifest located in buildDir and returns a
// list of Uploads for the images used for testing.
func ImageUploads(buildDir, namespace string) ([]Upload, error) {
	imageManifest := path.Join(buildDir, build.ImageManifestName)
	imgs, err := build.LoadImages(imageManifest)
	if err != nil {
		return nil, fmt.Errorf("failed to load images: %v", err)
	}
	// build.LoadImages makes all image paths absolute, so in order to get
	// the relative path to the build dir (to use as the relative path to the
	// images dir in GCS), we need to make sure buildDir is absolute as well.
	if !filepath.IsAbs(buildDir) {
		buildDir, err = filepath.Abs(buildDir)
		if err != nil {
			return nil, fmt.Errorf("failed to get absolute path to build dir: %v", err)
		}
	}
	files := []Upload{{
		Source:      imageManifest,
		Destination: filepath.Join(namespace, build.ImageManifestName)}}
	seen := make(map[string]bool)
	for _, img := range imgs {
		if isActualImage(img) {
			relPath, err := filepath.Rel(buildDir, img.Path)
			if err != nil {
				return nil, err
			}
			if _, ok := seen[img.Path]; !ok {
				files = append(files, Upload{
					Source:      img.Path,
					Destination: filepath.Join(namespace, relPath),
				})
				seen[img.Path] = true
			}
		}
	}
	return files, nil
}

func isActualImage(img build.Image) bool {
	return len(img.PaveArgs) > 0 || len(img.NetbootArgs) > 0 || len(img.PaveZedbootArgs) > 0 || img.Name == "qemu-kernel" || img.Name == "storage-full"
}
