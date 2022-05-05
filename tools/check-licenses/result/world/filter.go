// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

func (w *World) FilterProjects() error {
	projects := make([]*project.Project, 0)
	projectsMap := make(map[*project.Project]bool, 0)

	filepaths := make([]string, 0)
	var err error

	if isDir(Config.Target) {
		w.Status.WriteString(fmt.Sprintf("Filtering projects using a folder prefix check: `%s`\n", Config.Target))
		filepaths = w.FilterProjectsUsingDirectoryPrefix(filepaths)
	}

	filepaths, err = w.FilterExtraProjects(filepaths)
	if err != nil {
		return err
	}

	fileMap, err := w.getFileMap()
	if err != nil {
		return err
	}

	// Loop through the content, cleaning up the path strings
	// so we can map them to entries in our project map.
	for _, fp := range filepaths {
		if p, ok := fileMap[fp]; ok {
			projectsMap[p] = true
		}
	}

	for p := range projectsMap {
		projects = append(projects, p)
	}
	sort.Sort(project.Order(projects))

	w.FilteredProjects = projects

	filteredCount := len(w.FilteredProjects)
	totalCount := len(w.Projects)
	filteredOutCount := totalCount - filteredCount
	w.Status.WriteString(fmt.Sprintf("%v projects remain (filtered out %v from the full list of %v projects).\n", filteredCount, filteredOutCount, totalCount))
	return nil
}

func (w *World) FilterProjectsUsingDirectoryPrefix(filepaths []string) []string {
	for _, p := range w.Projects {
		for _, f := range p.Files {
			if strings.HasPrefix(f.Path, Config.Target) {
				filepaths = append(filepaths, f.Path)
			}
		}
	}
	return filepaths
}

func (w *World) FilterExtraProjects(filepaths []string) ([]string, error) {
	for _, projectFile := range Config.Filters {
		w.Status.WriteString(fmt.Sprintf("Adding additional projects from json file: `%s`\n", projectFile))

		f, err := os.Open(projectFile)
		if err != nil {
			return filepaths, err
		}
		defer f.Close()

		content, err := ioutil.ReadAll(f)
		if err != nil {
			return filepaths, err
		}

		projects := make([]string, 0)
		if err = json.Unmarshal(content, &projects); err != nil {
			return filepaths, fmt.Errorf("Failed to unmarshal project json file [%v]: %v\n", projectFile, err)
		}
		for _, p := range projects {
			filepaths = append(filepaths, p)
		}
	}

	return filepaths, nil
}

func (w *World) getFileMap() (map[string]*project.Project, error) {
	// Create a mapping that goes from file path to project,
	// so we can retrieve the projects that match dependencies in the
	// gn gen file.
	fileMap := make(map[string]*project.Project, 0)
	for _, p := range project.AllProjects {
		allFiles := make([]*file.File, 0)
		allFiles = append(allFiles, p.Files...)
		allFiles = append(allFiles, p.LicenseFile...)
		for _, f := range allFiles {
			path := f.Path
			if strings.Contains(f.Path, Config.FuchsiaDir) {
				var err error
				path, err = filepath.Rel(Config.FuchsiaDir, f.Path)
				if err != nil {
					return nil, err
				}
			}
			filePath := "//" + path
			folderPath := "//" + filepath.Dir(path)
			fileMap[filePath] = p
			fileMap[folderPath] = p
		}
	}
	return fileMap, nil
}

func isDir(str string) bool {
	_, err := os.Stat(str)
	if err == nil {
		return true
	}
	if os.IsNotExist(err) {
		return false
	}
	return false
}
