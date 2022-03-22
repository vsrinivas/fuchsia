// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"bytes"
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
	Files     []*file.File
	FileTrees []*filetree.FileTree
	Projects  []*project.Project
	Patterns  []*license.Pattern

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

// CSVData combines some header information along with the data for easily
// producing a CSV file.
//
// This just makes it easier for us to access the information from a template.
type CSVData struct {
	Header  string
	Entries []*CSVEntry
}

type CSVEntry struct {
	Project string
	Path    string
	Package string
	Left    string
	Right   string
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

	var err error
	diffTarget := []byte{}
	if Config.DiffNotice != "" {
		diffTarget, err = ioutil.ReadFile(Config.DiffNotice)
		if err != nil {
			return "", err
		}
	}

	diffHeader := []string{}
	if Config.DiffNotice != "" {
		diffHeader = []string{
			"Diffing local workspace against " + Config.DiffNotice,
		}
	}
	diffInfo := &DiffInfo{
		Content: diffTarget,
		Header:  diffHeader,
	}

	w := &World{
		Files:     allFiles,
		FileTrees: allFileTrees,
		Patterns:  allPatterns,
		Projects:  allProjects,

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

// index is the location in a byte slice where a certain text segment
// was found.
//
// newLines is the locations of all the newline characters in the entire
// file.
//
// This method simply returns the line number of the given piece of text,
// without us having to scan the entire file line-by-line.
func getLineNumber(index int, newlines []int) int {
	if index < 0 {
		return index
	}

	largest := 0
	for i, v := range newlines {
		if v == index {
			return i
		}
		if v > index {
			if i > 0 {
				return i - 1
			} else {
				return 0
			}
		}
		largest = i

	}
	return largest
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
	// Return a list of CSVEntries that can be used to easily
	// produce a CSV file, given the information stored in the World object.
	"getCSVEntries": func(w *World) *CSVData {
		csvData := &CSVData{}
		entries := make([]*CSVEntry, 0)
		newlines := make([]int, 0)
		for i := 0; i < len(w.Diff.Content); i++ {
			index := bytes.Index(w.Diff.Content[i:], []byte("\n"))
			if index < 0 {
				break
			}
			i = i + index
			newlines = append(newlines, i)
		}

		// If Config.DiffTarget is set, store information
		// about the diff target in the return value.
		sort.Sort(project.Order(w.Projects))
		numLicenses := 0
		numFound := 0
		numPartiallyFound := 0
		numMissing := 0
		for _, p := range w.Projects {
			sort.Sort(file.Order(p.LicenseFile))
			for _, l := range p.LicenseFile {
				sort.Sort(file.OrderFileData(l.Data))
				for _, d := range l.Data {
					numLicenses += 1
					status := "missing"

					// Search for the given license text in the target notice file.
					index := bytes.Index(w.Diff.Content, d.Data)
					ln := getLineNumber(index, newlines)
					if ln > -1 {
						numFound += 1
						status = fmt.Sprintf("line %v", ln)
					} else {
						// If it isn't found, perhaps the copyright header information
						// was extracted & printed elsewhere. Try searching again,
						// using only the license text (the bottom 80% of the filedata content).
						size := len(d.Data)
						start := int(0.2 * float64(size))
						index := bytes.Index(w.Diff.Content, d.Data[start:])
						ln := getLineNumber(index, newlines)
						if ln > -1 {
							numPartiallyFound += 1
							status = fmt.Sprintf("partial line %v", ln)
						} else {
							numMissing += 1
						}
					}

					e := &CSVEntry{
						Project: p.Name,
						Path:    l.Path,
						Package: d.LibraryName,
						Left:    fmt.Sprintf("line %v", d.LineNumber),
						Right:   status,
					}
					entries = append(entries, e)
				}
			}
		}
		csvData.Header = fmt.Sprintf("Found %v licenses out of %v, partial found %v [missing %v]", numFound, numLicenses, numPartiallyFound, numMissing)
		csvData.Entries = entries
		return csvData
	},
}
