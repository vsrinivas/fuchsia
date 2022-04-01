// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// The output of `fx gn gen --ide=json` is *verbose*. There is a lot of info
// that check-licenses doesn't really care about.
//
// GnGenContent is a struct that we use to hold just the information we
// do care about.
type GnGenContent struct {
	Targets map[string]*Target `json:"targets"`
}

// The fields that check-licenses cares about in the `fx gn gen` output
// appear to live in 3 main areas: sources, inputs and deps. This
// Target struct will hold that information for each entry in the output.
type Target struct {
	Sources []string `json:sources"`
	Inputs  []string `json:inputs"`
	Deps    []string `json:deps"`
}

// Parse the project.json output file.
func getGnGenProjects() ([]*project.Project, error) {
	content := &GnGenContent{
		Targets: make(map[string]*Target),
	}

	projects := make([]*project.Project, 0)
	projectsMap := make(map[*project.Project]bool, 0)
	if Config.GnGenOutputFile == "" {
		return projects, nil
	}

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
				return nil, err
			}
			filePath := "//" + path
			folderPath := "//" + filepath.Dir(path)
			fileMap[filePath] = p
			fileMap[folderPath] = p
		}
	}

	// Read in the projects.json file.
	//
	// This file can be really large (554MB on my machine), so we may
	// need to investigate streaming this data if it becomes a problem.
	b, err := ioutil.ReadFile(Config.GnGenOutputFile)
	if err != nil {
		return nil, fmt.Errorf("Failed to read projects.json file [%v]: %v\n", Config.GnGenOutputFile, err)
	}

	// Parse the content into a data structure we can operate on.
	if err = json.Unmarshal(b, content); err != nil {
		return nil, fmt.Errorf("Failed to unmarshal projects.json file [%v]: %v\n", Config.GnGenOutputFile, err)
	}

	// Loop through the content, cleaning up the path strings
	// so we can map them to entries in our project map.
	for _, t := range content.Targets {
		for index, s := range cleanBuildTargets(t.Sources) {
			if p, ok := fileMap[s]; ok {
				projectsMap[p] = true
			} else {
				plusVal(NotFoundInFileMap, fmt.Sprintf("%v [%v]", s, t.Sources[index]))
			}
		}
		for index, i := range cleanBuildTargets(t.Inputs) {
			if p, ok := fileMap[i]; ok {
				projectsMap[p] = true
			} else {
				plusVal(NotFoundInFileMap, fmt.Sprintf("%v [%v]", i, t.Inputs[index]))
			}
		}
		for index, d := range cleanBuildTargets(t.Deps) {
			if p, ok := fileMap[d]; ok {
				projectsMap[p] = true
			} else {
				plusVal(NotFoundInFileMap, fmt.Sprintf("%v [%v]", d, t.Deps[index]))
			}
		}
	}

	for p := range projectsMap {
		projects = append(projects, p)
	}
	sort.Sort(project.Order(projects))

	return projects, nil
}

func cleanBuildTargets(targets []string) []string {
	re := regexp.MustCompile(`-v\d_\d+_\d+`)

	result := make([]string, 0)
	for _, t := range targets {
		// We don't search the //out directory for licenses, so skip those targets here.
		if strings.HasPrefix(t, "//out") {
			continue
		}

		// Rust crate dependencies are all linked into the build system
		// using targets that are defined in the "rust_crates/BUILD.gn" file.
		//
		// We need to convert that target into a filepath.
		// Attempt to point to the correct filepath by converting the colon (:)
		// into "/vendor/" (since most rust_crates projects end up in that subdir).
		if strings.Contains(t, "rust_crates") {
			t = strings.ReplaceAll(t, ":", "/vendor/")
		}

		// If this target isn't a rust crate target, we still want to retrieve the relevant directory,
		// not the target name in that directory.
		// If a colon exists in this string, delete it and everything after it.
		t = strings.Split(t, ":")[0]

		// Same goes for toolchain definitions.
		// If a parenthesis exists in this string, delete it and everything after it.
		t = strings.Split(t, "(")[0]

		// Many rust crate libraries have a version string in their target name,
		// but no version string in their folder path. If we see this specific
		// version string pattern, remove it from the string.
		t = re.ReplaceAllString(t, "")

		result = append(result, t)
	}
	return result
}
