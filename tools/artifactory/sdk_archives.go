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

func SDKArchiveUploads(mods *build.Modules, namespace string) []Upload {
	return sdkArchiveUploads(mods, namespace)
}

func sdkArchiveUploads(mods sdkArchiveModules, namespace string) []Upload {
	var uploads []Upload
	for _, sdkArchive := range mods.SDKArchives() {
		var destination string
		if sdkArchive.OS == "fuchsia" {
			destination = path.Join(namespace, filepath.Base(sdkArchive.Path))
		} else {
			destination = path.Join(namespace, fmt.Sprintf("%s-%s", sdkArchive.OS, sdkArchive.CPU), filepath.Base(sdkArchive.Path))
		}
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), sdkArchive.Path),
			Destination: destination,
		})
	}
	return uploads
}

type sdkArchiveModules interface {
	BuildDir() string
	SDKArchives() []build.SDKArchive
}
