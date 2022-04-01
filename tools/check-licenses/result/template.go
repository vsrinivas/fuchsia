// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// World is a struct that contains all the information about this check-license
// run. The fields are all defined for easy use in template files.
type World struct {
	Files         []*file.File
	FileTrees     []*filetree.FileTree
	Projects      []*project.Project
	Patterns      []*license.Pattern
	GnGenProjects []*project.Project

	Diff *DiffInfo
}

// DiffInfo combines some header information along with the content of
// the NOTICE file that we are diffing against.
//
// This just makes it easier for us to access the information from a template.
type DiffInfo struct {
	Header  []string
	Content []byte
}

// Generate output files for every template defined in the config file.
func expandTemplates() (string, error) {
	if Config.OutputDir == "" {
		return "", nil
	}

	outDir := filepath.Join(Config.OutputDir, "out")
	if _, err := os.Stat(outDir); os.IsNotExist(err) {
		err := os.Mkdir(outDir, 0755)
		if err != nil {
			return "", err
		}
	}

	var b strings.Builder
	b.WriteString("\n")

	allPatterns := make([]*license.Pattern, 0)
	for _, p := range license.AllPatterns {
		if len(p.Matches) > 0 {
			allPatterns = append(allPatterns, p)
		}
	}
	sort.Sort(license.Order(allPatterns))

	allProjects := make([]*project.Project, 0)
	for _, p := range project.FilteredProjects {
		allProjects = append(allProjects, p)
	}
	sort.Sort(project.Order(allProjects))

	allFiles := make([]*file.File, 0)
	for _, f := range file.AllFiles {
		allFiles = append(allFiles, f)
	}
	sort.Sort(file.Order(allFiles))

	allFileTrees := make([]*filetree.FileTree, 0)
	for _, f := range filetree.AllFileTrees {
		allFileTrees = append(allFileTrees, f)
	}
	sort.Sort(filetree.Order(allFileTrees))

	gnGenProjects, err := getGnGenProjects()
	if err != nil {
		return "", err
	}
	sort.Sort(project.Order(gnGenProjects))

	diffTarget := []byte{}
	diffHeader := []string{}
	if Config.DiffNotice != "" {
		diffTarget, err = ioutil.ReadFile(Config.DiffNotice)
		if err != nil {
			return "", err
		}
		diffHeader = []string{"Diffing local workspace against " + Config.DiffNotice}
	}
	diffInfo := &DiffInfo{
		Content: diffTarget,
		Header:  diffHeader,
	}

	w := &World{
		Files:         allFiles,
		FileTrees:     allFileTrees,
		Patterns:      allPatterns,
		Projects:      allProjects,
		GnGenProjects: gnGenProjects,

		Diff: diffInfo,
	}

	for _, o := range Config.Outputs {
		if t, ok := AllTemplates[o]; !ok {
			return "", fmt.Errorf("Couldn't find template %v\n", o)
		} else {
			name := filepath.Join(outDir, o)
			f, err := os.Create(name)
			if err != nil {
				return "", err
			}
			if err := t.Execute(f, w); err != nil {
				return "", err
			}
			if Config.Zip {
				if err := compressGZ(name); err != nil {
					return "", err
				}
			}
			b.WriteString(fmt.Sprintf(" ⦿ Executed template -> %v", name))
			if Config.Zip {
				b.WriteString(" (+ *.gz)")
			}
			b.WriteString("\n")
		}
	}
	return b.String(), nil
}

var templateFunctions = template.FuncMap{
	// Return a list of CSVEntries that can be used to easily
	// produce a CSV file, given the information stored in the World object.
	"getCSVEntries": getCSVEntries,

	// List projects together that have the same exact license texts.
	"getDedupedPatterns": getDedupedPatterns,
}
