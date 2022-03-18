// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"fmt"
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
	for _, p := range project.AllProjects {
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

	w := &World{
		Files:     allFiles,
		FileTrees: allFileTrees,
		Patterns:  allPatterns,
		Projects:  allProjects,
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
	// Remove duplicate values from a list of strings.
	"uniqueList": func(input []*file.FileData) []string {
		m := make(map[string]bool)
		for _, s := range input {
			m[s.LibraryName] = true
		}

		output := make([]string, 0)
		for k := range m {
			output = append(output, k)
		}
		sort.Strings(output)

		return output
	},
	"getCSVEntries": func(w *World) []string {
		return []string{}
	},
}
