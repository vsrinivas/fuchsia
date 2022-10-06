// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/util"
)

// Filter out projects that do not contribute to the build graph.
func FilterProjects() error {
	var filepaths []string
	var err error

	// Get dependencies of the target or current workspace.
	if filepaths, err = getDependencies(); err != nil {
		return err
	}

	// Get a mapping of file to project.
	var fileMap map[string]*project.Project
	fileMap, err = getFileMap()
	if err != nil {
		return err
	}

	projectsMap := make(map[string]*project.Project, 0)
	for _, fp := range filepaths {
		if p, ok := fileMap[fp]; ok {
			projectsMap[p.Root] = p
		}
	}
	project.FilteredProjects = projectsMap

	log.Printf(" -> %v projects remain (from the full list of %v projects).\n",
		len(projectsMap),
		len(project.AllProjects))
	return nil
}

func getDependencies() ([]string, error) {
	// Run "fx gn <>" command to generate a filter file.
	gn, err := util.NewGn(*gnPath, *buildDir)
	if err != nil {
		return nil, err
	}

	filterDir := filepath.Join(*outDir, "filter")
	gnFilterFile := filepath.Join(filterDir, "gnFilter.json")
	if _, err := os.Stat(filterDir); os.IsNotExist(err) {
		err := os.Mkdir(filterDir, 0755)
		if err != nil {
			return nil, fmt.Errorf("Failed to create filter directory [%v]: %v\n",
				filterDir, err)
		}
	}

	if Config.Target != "" {
		log.Printf(" -> Running 'fx gn desc %v' command...", Config.Target)
		if content, err := gn.Dependencies(context.Background(),
			gnFilterFile, Config.Target); err != nil {
			return nil, err
		} else {
			buf := bytes.NewBufferString("")
			for _, s := range content {
				if _, err := buf.WriteString(s + "\n"); err != nil {
					return nil, fmt.Errorf("Failed to write 'gn desc' buffer string: %v", err)
				}
			}
			plusFile("GnDescUnpacked.txt", buf.Bytes())
			return content, nil
		}
	}

	log.Print(" -> Running 'fx gn gen' command...")
	if content, err := gn.Gen(context.Background(), gnFilterFile); err != nil {
		return nil, err
	} else {
		buf := bytes.NewBufferString("")
		for _, s := range content {
			if _, err := buf.WriteString(s + "\n"); err != nil {
				return nil, fmt.Errorf("Failed to write `gn gen` buffer string: %v", err)
			}
		}
		plusFile("GnGenUnpacked.txt", buf.Bytes())
		return content, nil
	}
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
