// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
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
		if strings.Contains(strings.ToLower(item.Name()), "licen") &&
			!strings.Contains(strings.ToLower(item.Name()), "tmpl") {
			licenseFilePaths = append(licenseFilePaths, item.Name())
		}
	}

	if len(licenseFilePaths) == 0 {
		return nil, os.ErrNotExist
	}

	p := &Project{
		Name:               projectName,
		Root:               projectRootPath,
		LicenseFileType:    file.SingleLicense,
		RegularFileType:    file.Any,
		ShouldBeDisplayed:  true,
		SourceCodeIncluded: false,
	}

	for _, l := range licenseFilePaths {
		l = filepath.Join(p.Root, l)
		l = filepath.Clean(l)

		licenseFile, err := file.NewFile(l, p.LicenseFileType)
		if err != nil {
			return nil, err
		}
		p.LicenseFile = append(p.LicenseFile, licenseFile)

		if strings.Contains(l, "dart-pkg") {
			licenseFile.Url = fmt.Sprintf("https://pub.dev/packages/%v/license", projectName)
		}

		if strings.Contains(l, "golibs") {
			re := regexp.MustCompile(`(.*golibs\/vendor\/)`)
			url := re.ReplaceAllString(l, "")
			url = strings.ReplaceAll(url, "/LICENSE", "")
			url = fmt.Sprintf("https://pkg.go.dev/%v?tab=licenses", url)
			licenseFile.Url = url
		}

		if strings.Contains(l, "rust_crates") {
			rel := l
			if strings.Contains(l, Config.FuchsiaDir) {
				rel, _ = filepath.Rel(Config.FuchsiaDir, l)
			}
			licenseFile.Url = fmt.Sprintf("https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/%v", rel)
		}
	}

	plusVal(NumProjects, p.Root)
	AllProjects[p.Root] = p

	return p, nil
}
