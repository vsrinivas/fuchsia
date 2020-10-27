// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"archive/tar"
	"log"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

const (
	// uefiImageName is the canonical name of an x64 UEFI image in the
	// manifest.
	uefiImageName = "uefi-disk"
	// gceUploadName is the canonical name of the uploaded GCE image.
	gceUploadName = "disk.tar.gz"
	// gceImageName is the canonical expected name of a source image in GCE.
	gceImageName = "disk.raw"
)

// ImageUploads parses the image manifest located in the build and returns a
// list of Uploads for the images used for testing.
func ImageUploads(mods *build.Modules, namespace string) ([]Upload, error) {
	return imageUploads(mods, namespace)
}

func imageUploads(mods imgModules, namespace string) ([]Upload, error) {
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
			if img.Name == uefiImageName {
				srcPath := filepath.Join(mods.BuildDir(), img.Path)
				info, err := os.Stat(srcPath)
				if err != nil {
					log.Printf("failed to stat gce image on disk: %s", err)
					continue
				}
				dest := filepath.Join(filepath.Dir(img.Path), gceUploadName)
				files = append(files, Upload{
					Source:      srcPath,
					Destination: path.Join(namespace, dest),
					Compress:    true,
					TarHeader: &tar.Header{
						Format: tar.FormatGNU,
						Name:   gceImageName,
						Mode:   0666,
						Size:   info.Size(),
					},
				})
			} else {
				files = append(files, Upload{
					Source:      filepath.Join(mods.BuildDir(), img.Path),
					Destination: path.Join(namespace, img.Path),
					Compress:    true,
				})
			}
			seen[img.Path] = struct{}{}
		}
	}
	return files, nil
}

type imgModules interface {
	BuildDir() string
	Images() []build.Image
	ImageManifest() string
}
