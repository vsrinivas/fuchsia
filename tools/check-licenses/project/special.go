// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
)

// NewSpecialProject creates a Project object from a directory.
//
// There are hundreds of rust_crate projects that don't contain a README.fuchsia file.
// The same goes for golibs and dart_pkg project. We can't expect these 3p projects
// to maintain a README file specifically for fuchsia license detection.
//
// We handle them special here by automatically creating a project object based at the
// root of the project. This assumes all license files are named LICENSE.* or LICENCE.*
// which, in practice, has been correct.
func NewSpecialProject(projectRootPath string) (*Project, error) {
	var err error
	licenseFilePaths := make([]string, 0)

	projectName := filepath.Base(projectRootPath)

	directoryContents, err := os.ReadDir(projectRootPath)
	if err != nil {
		return nil, err
	}

	for _, item := range directoryContents {
		if item.IsDir() {
			continue
		}
		if strings.Contains(strings.ToLower(item.Name()), "licen") {
			licenseFilePaths = append(licenseFilePaths, item.Name())
		}
	}

	if len(licenseFilePaths) == 0 {
		return nil, os.ErrNotExist
	}

	p := &Project{
		Name:            projectName,
		Root:            projectRootPath,
		LicenseFileType: file.SingleLicense,
		RegularFileType: file.Any,
	}

	for _, l := range licenseFilePaths {
		l = filepath.Join(p.Root, l)
		l = filepath.Clean(l)

		licenseFile, err := file.NewFile(l, p.LicenseFileType)
		if err != nil {
			return nil, err
		}
		p.LicenseFile = append(p.LicenseFile, licenseFile)
	}

	plusVal(NumProjects, p.Root)
	AllProjects[p.Root] = p

	shouldInclude, err := Config.shouldInclude(p)
	if err != nil {
		return nil, err
	}
	if shouldInclude {
		plusVal(NumFilteredProjects, p.Root)
		FilteredProjects[p.Root] = p
	} else {
		plusVal(NumSkippedProjects, p.Root)
	}
	return p, nil
}
