// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"fmt"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/directory"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// World is a struct that contains all the information about this check-license
// run. The fields are all defined for easy use in template files.
type World struct {
	Status *strings.Builder

	Files            []*file.File
	Directories      []*directory.Directory
	Projects         []*project.Project
	Patterns         []*license.Pattern
	FilteredProjects []*project.Project

	Diff *DiffInfo
}

func NewWorld() (*World, error) {
	allPatterns := make([]*license.Pattern, 0)
	for _, p := range license.AllPatterns {
		if len(p.Matches) > 0 {
			allPatterns = append(allPatterns, p)
		}
	}
	sort.Sort(license.Order(allPatterns))

	allProjects := make([]*project.Project, 0)
	for _, p := range project.AllProjects {
		sort.SliceStable(p.SearchResults, func(i, j int) bool {
			return string(p.SearchResults[i].LicenseData.Data) < string(p.SearchResults[j].LicenseData.Data)
		})
		allProjects = append(allProjects, p)
	}
	sort.Sort(project.Order(allProjects))

	allFiles := make([]*file.File, 0)
	for _, f := range file.AllFiles {
		allFiles = append(allFiles, f)
	}
	sort.Sort(file.Order(allFiles))

	allDirectories := make([]*directory.Directory, 0)
	for _, f := range directory.AllDirectories {
		allDirectories = append(allDirectories, f)
	}
	sort.Sort(directory.Order(allDirectories))

	w := &World{
		Status:      &strings.Builder{},
		Files:       allFiles,
		Directories: allDirectories,
		Patterns:    allPatterns,
		Projects:    allProjects,
	}

	if err := w.FilterProjects(); err != nil {
		return nil, err
	}

	if err := w.AddLicenseUrls(); err != nil {
		return nil, err
	}

	if Config.DiffNotice != "" {
		w.Status.WriteString(fmt.Sprintf("Diffing current workspace against `%s`\n", Config.DiffNotice))
		w.SetDiffInfo()
	}

	return w, nil
}
