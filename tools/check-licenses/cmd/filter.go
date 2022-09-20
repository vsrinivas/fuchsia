// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

func FilterProjects() error {
	projects := make([]*project.Project, 0)
	projectsMap := make(map[*project.Project]bool, 0)

	filepaths := make([]string, 0)
	var err error

	if isDir(Config.Target) {
		log.Printf(fmt.Sprintf(" -> Filtering projects using a folder prefix check: `%s`\n", Config.Target))
		filepaths = FilterProjectsUsingDirectoryPrefix(filepaths)
	}

	filepaths, err = FilterExtraProjects(filepaths)
	if err != nil {
		return err
	}

	fileMap, err := getFileMap()
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

	for _, p := range projects {
		project.FilteredProjects[p.Root] = p
	}

	filteredCount := len(project.FilteredProjects)
	totalCount := len(project.AllProjects)
	filteredOutCount := totalCount - filteredCount
	log.Printf(" -> %v projects remain (filtered out %v from the full list of %v projects).\n", filteredCount, filteredOutCount, totalCount)
	return nil
}

func FilterProjectsUsingDirectoryPrefix(filepaths []string) []string {
	for _, p := range project.AllProjects {
		for _, f := range p.Files {
			if strings.HasPrefix(f.AbsPath, Config.Target) {
				filepaths = append(filepaths, f.AbsPath)
			}
		}
	}
	return filepaths
}

func FilterExtraProjects(filepaths []string) ([]string, error) {
	for _, projectFile := range Config.Filters {
		log.Printf(" -> Adding additional projects from json file: `%s`\n", projectFile)

		f, err := os.Open(projectFile)
		if err != nil {
			return filepaths, err
		}
		defer f.Close()

		content, err := io.ReadAll(f)
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

func getFileMap() (map[string]*project.Project, error) {
	// Create a mapping that goes from file path to project,
	// so we can retrieve the projects that match dependencies in the
	// gn gen file.
	fileMap := make(map[string]*project.Project, 0)
	for _, p := range project.AllProjects {
		allFiles := make([]*file.File, 0)
		allFiles = append(allFiles, p.Files...)
		allFiles = append(allFiles, p.LicenseFile...)
		for _, f := range allFiles {
			path := f.AbsPath
			if strings.Contains(f.AbsPath, Config.FuchsiaDir) {
				var err error
				path, err = filepath.Rel(Config.FuchsiaDir, f.AbsPath)
				if err != nil {
					return nil, err
				}
			}
			filePath := "//" + path
			folderPath := "//" + filepath.Dir(path)

			// "gn gen" may reveal that the current workspace has a a dependency on a LICENSE file.
			// That LICENSE file may be used in two or more different projects across fuchsia.git.
			// There's no way for us to tell which project actually contributes to the build.
			//
			// We want to deterministically generate the final NOTICE file, so in this situation
			// we simply choose the project that comes first alphabetically.
			//
			// In practice this simple strategy should be OK. "gn desc" / "gn gen" will undoubtedly
			// also have dependencies on other files in the project, which will ensure that the correct
			// project is included (even if we also occasionally include an unrelated one).
			if otherP, ok := fileMap[filePath]; ok {
				if p.Root < otherP.Root {
					fileMap[filePath] = p
					fileMap[folderPath] = p
				}
			} else {
				fileMap[filePath] = p
				fileMap[folderPath] = p
			}
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
