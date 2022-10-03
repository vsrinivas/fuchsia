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

	switch {
	case strings.Contains(p.Root, "dart-pkg"):
		p.URL = fmt.Sprintf("https://pub.dev/packages/%v", projectName)

	case strings.Contains(p.Root, "golibs"):
		re := regexp.MustCompile(`(.*golibs\/vendor\/)`)
		url := re.ReplaceAllString(p.Root, "")
		url = strings.ReplaceAll(url, "/LICENSE", "")
		p.URL = fmt.Sprintf("https://pkg.go.dev/%v", url)

	case strings.Contains(p.Root, "rust_crates"):
		url, err := git.GetURL(ctx, p.Root)
		if err != nil {
			return nil, err
		}
		hash, err := git.GetCommitHash(ctx, p.Root)
		if err != nil {
			return nil, err
		}
		p.URL = fmt.Sprintf("%v/+/%v", url, hash)
	}

	for _, l := range licenseFilePaths {
		l = filepath.Join(p.Root, l)
		l = filepath.Clean(l)

		licenseFile, err := file.NewFile(l, p.LicenseFileType)
		if err != nil {
			return nil, err
		}
		p.LicenseFile = append(p.LicenseFile, licenseFile)

		switch {
		case strings.Contains(l, "dart-pkg"):
			licenseFile.URL = fmt.Sprintf("%v/license", p.URL)

		case strings.Contains(l, "golibs"):
			licenseFile.URL = fmt.Sprintf("%v?tab=licenses", p.URL)

		case strings.Contains(l, "rust_crates"):
			rel := l
			if strings.Contains(l, Config.FuchsiaDir) {
				rel, _ = filepath.Rel(Config.FuchsiaDir, l)
			}
			licenseFile.URL = fmt.Sprintf("https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/%v", rel)
		}
	}

	plusVal(NumProjects, p.Root)
	plusVal(ProjectURLs, fmt.Sprintf("%v - %v", p.Root, p.URL))
	AllProjects[p.Root] = p
	return p, nil
}
