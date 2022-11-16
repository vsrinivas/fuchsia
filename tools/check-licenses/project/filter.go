// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"context"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/util"
)

// Using the AllProjects map and the output of "fx gn gen",
// filter out all projects that don't appear in the dependency tree of
// Config.Target.
func FilterProjects() error {
	// Acquire a handle to the "gn" binary on the local workstation.
	gn, err := util.NewGn(Config.GnPath, Config.BuildDir)
	if err != nil {
		return err
	}

	// Run "fx gn <>" command, and retrieve the output data.
	gen, err := gn.Gen(context.Background(), Config.Target)
	if err != nil {
		return err
	}

	// Generate a map:
	//   [filepath for every file in project X] -> [Project X]
	// With this mapping, we can match GN targets and file inputs
	// to check-license Project structs.
	fileMap, err := getFileMap()
	if err != nil {
		return err
	}

	// Find Projects that match each target in the dependency tree.
	_, err = processGenOutput(gen, fileMap)
	if err != nil {
		return err
	}

	return nil
}

func processGenOutput(gen *util.Gen, fileMap map[string]*Project) (*Project, error) {
	for _, t := range gen.FilteredTargets {
		var project *Project
		var ok bool
		for _, possibleProjectName := range t.CleanNames {
			if project, ok = fileMap[possibleProjectName]; ok {
				// Project 'project' represents GN target 't'.
				// Break out of this loop and proceed.
				break
			}
		}
		if project == nil {
			// Some directories (e.g. test directories) are skipped,
			// so projects won't be found for those files.
			// TODO(jcecil): Make this a failing error.
			// return nil, fmt.Errorf("Failed to find project matching name %v\n", t.CleanNames)
			continue
		}

		// Use the same process on t.CleanDeps to find all projects
		// that match the GN target 't''s dependencies.
		// Add those projects to the project.Children map.
		for _, d := range t.CleanDeps {
			if child, ok := fileMap[d]; ok && child.Root != project.Root {
				project.Children[child.Root] = child
				FilteredProjects[child.Root] = child
			}
		}

		FilteredProjects[project.Root] = project
	}

	return nil, nil
}

func getFileMap() (map[string]*Project, error) {
	// Create a mapping that goes from file path to project,
	// so we can retrieve the projects that match dependencies in the
	// gn gen file.
	fileMap := make(map[string]*Project, 0)
	for _, p := range AllProjects {
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

			// "gn gen" may reveal that the current workspace
			// has a dependency on a LICENSE file.
			// That LICENSE file may be used in two or more
			// different projects across fuchsia.git.
			// There's no way for us to tell which project
			// actually contributes to the build.
			//
			// We want to deterministically generate the final
			// NOTICE file, so in this situation we simply choose
			// the project that comes first alphabetically.
			//
			// In practice this simple strategy should be OK.
			// "gn desc" / "gn gen" will undoubtedly also have
			// dependencies on other files in the project, which
			// will ensure that the correct project is included
			// (even if we occasionally include an unrelated one).
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
