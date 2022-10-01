// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"bytes"
	"fmt"
	"path/filepath"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// CSVData combines some header information along with the data for easily
// producing a CSV file.
//
// This just makes it easier for us to access the information from a template.
type CSVData struct {
	Header  string
	Entries []*CSVEntry
}

type CSVEntry struct {
	Project     string
	Path        string
	LicenseType string
	Url         string
	Package     string
	Left        string
	Right       string

	// For compliance worksheet
	BeingSurfaced      string
	SourceCodeIncluded string
}

func NewCSVEntry() {
}

func (csv *CSVEntry) Merge(other *CSVEntry) {
	if !strings.Contains(csv.LicenseType, other.LicenseType+";") {
		csv.LicenseType = fmt.Sprintf("%v; %v", csv.LicenseType, other.LicenseType)
	}
	if !strings.Contains(csv.Package, other.Package+";") {
		csv.Package = fmt.Sprintf("%v; %v", csv.Package, other.Package)
	}
	csv.Left = fmt.Sprintf("%v; %v", csv.Left, other.Left)
}

func (w *World) GetMergedCSVEntries() *CSVData {
	data := w.GetCSVEntries()

	csvMap := make(map[string]*CSVEntry, 0)
	for _, left := range data.Entries {
		if right, ok := csvMap[left.Path]; ok {
			right.Merge(left)
		} else {
			csvMap[left.Path] = left
		}
	}

	newEntries := make([]*CSVEntry, 0)
	for _, csv := range csvMap {
		if csv.Package == csv.Project {
			csv.Project = "Various"
		}
		newEntries = append(newEntries, csv)
	}
	sort.Slice(newEntries, func(i, j int) bool {
		return newEntries[i].Path < newEntries[j].Path
	})
	data.Entries = newEntries
	return data
}

func (w *World) GetCSVEntries() *CSVData {
	csvData := &CSVData{}
	entries := make([]*CSVEntry, 0)

	// If world.Diff exists, it means we want to diff the NOTICE file from the current workspace
	// against the diff file specified in Config.DiffTarget.
	//
	// In order to provide line numbers for each match in DiffTarget, find all of the line breaks
	// in the target file by searching for newline characters.
	newlines := make([]int, 0)
	if w.Diff != nil {
		for i := 0; i < len(w.Diff.Content); i++ {
			index := bytes.Index(w.Diff.Content[i:], []byte("\n"))
			if index < 0 {
				break
			}
			i = i + index
			newlines = append(newlines, i)
		}
	}

	// If Config.DiffTarget is set, store information
	// about the diff target in the return value.
	sort.Sort(project.Order(w.FilteredProjects))
	numLicenses := 0
	numFound := 0
	numPartiallyFound := 0
	numMissing := 0
	for _, p := range w.FilteredProjects {
		sort.Sort(file.Order(p.LicenseFile))

		for _, l := range p.LicenseFile {
			sort.Sort(file.OrderFileData(l.Data))

			for _, d := range l.Data {
				numLicenses += 1
				e := &CSVEntry{
					Project:            p.Name,
					Path:               l.AbsPath,
					Package:            d.LibraryName,
					LicenseType:        d.LicenseType,
					BeingSurfaced:      "Yes",
					SourceCodeIncluded: "No",
				}

				if e.Package == "" {
					e.Package = e.Project
				}

				if strings.Contains(e.Path, Config.FuchsiaDir) {
					e.Path, _ = filepath.Rel(Config.FuchsiaDir, e.Path)
				}

				if !p.ShouldBeDisplayed {
					e.BeingSurfaced = "No"
				}

				if p.SourceCodeIncluded {
					e.SourceCodeIncluded = "Yes"
				}

				if l.Url != "" {
					e.Url = fmt.Sprintf(`=HYPERLINK("%v", "%v")`, l.Url, e.Path)
				}

				e.Left = fmt.Sprintf("line %v", d.LineNumber)
				if w.Diff != nil {
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

						// It's possible the bottom 80% of the text matches in several places
						// in the NOTICE file. Keep track of them all by using a "partials" slice.
						partials := make([]int, 0)
						index := bytes.Index(w.Diff.Content, d.Data[start:])
						base := 0
						for index > 0 {
							ln := getLineNumber(index+base, newlines)
							if ln > -1 {
								partials = append(partials, ln)
							}
							base = base + index
							index = bytes.Index(w.Diff.Content[base+1:], d.Data[start:])
						}
						if len(partials) > 0 {
							numPartiallyFound += 1
							status = fmt.Sprintf("partial line ")
							for _, i := range partials {
								status += fmt.Sprintf("%v | ", i)
							}
						} else {
							numMissing += 1
						}
					}

					e.Right = status
				}
				entries = append(entries, e)
			}
		}
	}

	if w.Diff != nil {
		csvData.Header = fmt.Sprintf("Found %v licenses out of %v | partial found %v [missing %v]", numFound, numLicenses, numPartiallyFound, numMissing)
	}
	sort.Slice(entries, func(i, j int) bool {
		return entries[i].Path < entries[j].Path
	})
	csvData.Entries = entries
	return csvData
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
