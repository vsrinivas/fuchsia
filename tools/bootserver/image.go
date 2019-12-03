// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Image is a fuchsia image as viewed by bootserver; a simplified version of build.Image.
type Image struct {
	// Name is an identifier for this image that usually derives from its target partition.
	// TODO(fxbug.dev/38517): Remove when BootZedbootShim is deprecated.
	Name string
	// Path is the location of the image on disk.
	Path string
	// Args correspond to the bootserver args that map to this image type.
	Args []string
}

// ConvertFromBuildImages filters and returns Images corresponding to build Images of a given bootMode.
// Filters and returns Images corresponding to build Images of a given bootMode
func ConvertFromBuildImages(buildImages []build.Image, bootMode Mode) []Image {
	var imgs []Image
	for _, buildImg := range buildImages {
		var args []string
		switch bootMode {
		case ModePave:
			args = buildImg.PaveArgs
		case ModeNetboot:
			args = buildImg.NetbootArgs
		case ModePaveZedboot:
			args = buildImg.PaveZedbootArgs
		}
		imgs = append(imgs, Image{
			Name: buildImg.Name,
			Path: buildImg.Path,
			Args: args,
		})
	}
	return imgs
}
