// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"context"
	"fmt"
	"path/filepath"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

func (w *World) FilterProjects() error {
	if isDir(Config.Target) {
		w.Status.WriteString(fmt.Sprintf("Filtering projects using a folder prefix check: `%s`\n", Config.Target))
		if err := w.FilterProjectsUsingDirectoryPrefix(); err != nil {
			return err
		}
	} else {
		gn, err := NewGn(Config.GnPath, Config.BuildDir)
		if err != nil {
			return err
		}
		if isTarget(Config.Target) {
			w.Status.WriteString(fmt.Sprintf("Filtering projects to a single gn target using `gn desc %s`\n", Config.Target))
			gndeps, err := gn.Dependencies(context.Background())
			if err != nil {
				return err
			}
			if err := w.FilterProjectsUsingGn(gn, gndeps); err != nil {
				return err
			}
		} else {
			w.Status.WriteString(fmt.Sprintf("Filtering projects to the entire workspace using `gn gen%s`\n", Config.Target))
			gngen, err := gn.Gen(context.Background())
			if err != nil {
				return err
			}
			if err := w.FilterProjectsUsingGn(gn, gngen); err != nil {
				return err
			}
		}
	}

	filteredCount := len(w.FilteredProjects)
	totalCount := len(w.Projects)
	filteredOutCount := totalCount - filteredCount
	w.Status.WriteString(fmt.Sprintf("%v projects remain (filtered out %v from the full list of %v projects).\n", filteredCount, filteredOutCount, totalCount))
	return nil
}

func (w *World) FilterProjectsUsingDirectoryPrefix() error {
	w.FilteredProjects = make([]*project.Project, 0)
OUTER:
	for _, p := range w.Projects {
		for _, f := range p.Files {
			if strings.HasPrefix(f.Path, Config.Target) {
				w.FilteredProjects = append(w.FilteredProjects, p)
				continue OUTER
			}
		}
	}
	sort.Sort(project.Order(w.FilteredProjects))
	return nil
}

// Parse the project.json output file.
func (w *World) FilterProjectsUsingGn(gn *Gn, gnDeps *GnDeps) error {

	projects := make([]*project.Project, 0)
	projectsMap := make(map[*project.Project]bool, 0)

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
					return err
				}
			}
			filePath := "//" + path
			folderPath := "//" + filepath.Dir(path)
			fileMap[filePath] = p
			fileMap[folderPath] = p
		}
	}

	// Loop through the content, cleaning up the path strings
	// so we can map them to entries in our project map.

	for k, t := range gnDeps.Targets {
		k = gn.LabelToDirectory(k)
		if p, ok := fileMap[k]; ok {
			projectsMap[p] = true
		}
		for _, s := range t.Sources {
			s = gn.LabelToDirectory(s)
			if p, ok := fileMap[s]; ok {
				projectsMap[p] = true
			}
		}
		for _, i := range t.Inputs {
			i = gn.LabelToDirectory(i)
			if p, ok := fileMap[i]; ok {
				projectsMap[p] = true
			}

		}
		for _, d := range t.Deps {
			d = gn.LabelToDirectory(d)
			if p, ok := fileMap[d]; ok {
				projectsMap[p] = true
			}
		}
	}
	for p := range projectsMap {
		projects = append(projects, p)
	}
	sort.Sort(project.Order(projects))

	w.FilteredProjects = projects

	return nil
}

func (w *World) getGnFileMap() (map[string]*project.Project, error) {
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
