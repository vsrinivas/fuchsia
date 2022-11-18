// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// LicenseUploads reads the licenses build API module and returns the
// list of Uploads.
func LicenseUploads(mods *build.Modules, namespace string) ([]Upload, error) {
	return licenseUploads(mods, namespace)
}

func licenseUploads(mods licModules, namespace string) ([]Upload, error) {

	files := []Upload{}

	for _, lic := range mods.Licenses() {
		// Compliance file
		files = append(files, Upload{
			Source:      path.Join(mods.BuildDir(), lic.ComplianceFile),
			Destination: path.Join(namespace, lic.ComplianceFile),
		})

		if lic.LicenseFilesDir != "" {
			// License files
			files = append(files, Upload{
				Source:      path.Join(mods.BuildDir(), lic.LicenseFilesDir),
				Destination: path.Join(namespace, "texts"),
				Recursive:   true,
			})
		}

		if lic.RunFilesArchive != "" {
			// Runfiles
			files = append(files, Upload{
				Source:      path.Join(mods.BuildDir(), lic.RunFilesArchive),
				Destination: path.Join(namespace, lic.RunFilesArchive),
			})
		}
	}
	return files, nil
}

type licModules interface {
	BuildDir() string
	Licenses() []build.License
}
