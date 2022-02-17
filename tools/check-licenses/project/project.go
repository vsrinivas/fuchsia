// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

var AllProjects []*Project

// Project struct follows the format of README.fuchsia files.
// For more info, see the following article:
//   https://fuchsia.dev/fuchsia-src/development/source_code/third-party-metadata
type Project struct {
	Root  string
	Files []string

	Name               string
	URL                string
	Version            string
	License            string
	LicenseFile        []string
	UpstreamGit        string
	Description        string
	LocalModifications string
}

// NewProject creates a Project object from a README.fuchsia file.
func NewProject(path string) (*Project, error) {
	p := &Project{
		Root: filepath.Dir(path),
	}

	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	s := bufio.NewScanner(file)
	s.Split(bufio.ScanLines)

	multiline := ""
	for s.Scan() {
		var line = s.Text()
		if strings.HasPrefix(line, "Name:") {
			p.Name = strings.TrimSpace(strings.TrimPrefix(line, "Name:"))
			multiline = ""
		} else if strings.HasPrefix(line, "URL:") {
			p.URL = strings.TrimSpace(strings.TrimPrefix(line, "URL:"))
			multiline = ""
		} else if strings.HasPrefix(line, "Version:") {
			p.Version = strings.TrimSpace(strings.TrimPrefix(line, "Version:"))
			multiline = ""
		} else if strings.HasPrefix(line, "License:") {
			p.License = strings.TrimSpace(strings.TrimPrefix(line, "License:"))
			multiline = ""
		} else if strings.HasPrefix(line, "License File:") {
			p.LicenseFile = append(p.LicenseFile, strings.TrimSpace(strings.TrimPrefix(line, "License File:")))
			multiline = ""
		} else if strings.HasPrefix(line, "Upstream Git:") {
			p.UpstreamGit = strings.TrimSpace(strings.TrimPrefix(line, "Upstream Git:"))
			multiline = ""
		} else if strings.HasPrefix(line, "Description:") {
			multiline = "Description"
		} else if strings.HasPrefix(line, "Local Modifications:") {
			multiline = "Local Modifications"
		}

		if multiline == "Description" {
			p.Description += strings.TrimSpace(strings.TrimPrefix(line, "Description:")) + "\n"
		} else if multiline == "Local Modifications" {
			p.LocalModifications += strings.TrimSpace(strings.TrimPrefix(line, "Local Modifications:")) + "\n"
		}
	}

	// All projects must have a name.
	if p.Name == "" {
		plusVal(MissingName, p.Root)
		if !Config.ContinueOnError {
			return nil, fmt.Errorf("Project %v is missing a name.", p.Root)
		}
	}

	// All projects must point to a license file.
	if len(p.LicenseFile) == 0 {
		plusVal(MissingLicenseFile, p.Root)
		if !Config.ContinueOnError {
			return nil, fmt.Errorf("Project %v is missing a license file.", p.Root)
		}
	}

	plus1(NumProjects)
	AllProjects = append(AllProjects, p)
	return p, nil
}

func (p *Project) AddFiles(files []string) {
	p.Files = append(p.Files, files...)
}
