// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"sort"
)

// Templates that print out the license texts for every project in aggregate
// can be very large. On my machine: 35MB uncompressed (~5MB compressed).
//
// One way to reduce the size is to deduplicate the license texts that
// match exactly. This file defines some data structures and functions
// to dedup the content in a World object for deduped templates.

type LicenseTextsGroupedByPatternType struct {
	// Pattern type (e.g. BSD, MIT)
	Type string

	// Map of ProjectTextMap structs.
	// This map is keyed on the hash of the license text.
	Map map[string]*ProjectTextMap

	// Sorted list to produce a deterministic result.
	Sorted []*ProjectTextMap
}
type ProjectTextMap struct {
	// License text content.
	LicenseText []byte

	// Project names that this license text appears in.
	ProjectNames map[string]bool

	// File names that contain this license text.
	FileNames map[string]bool

	// Sorted ists to produce deterministic results.
	SortedProjectNames []string
	SortedFileNames    []string
}

func getDedupedPatterns(w *World) []*LicenseTextsGroupedByPatternType {

	// Create a mapping that goes from file path to project,
	// so we can retrieve the projects that match the files
	// that we are looping over.
	allProjects := w.Projects
	if len(w.GnGenProjects) > 0 {
		allProjects = w.GnGenProjects
	}
	fileMap := make(map[string]string, 0)
	for _, p := range allProjects {
		for _, f := range p.Files {
			fileMap[f.Path] = p.Name
		}
		for _, f := range p.LicenseFile {
			fileMap[f.Path] = p.Name
		}
	}

	// Start by enumerating all of the different license types
	// (BSD, MIT, etc) that we use in this run
	types := make(map[string]*LicenseTextsGroupedByPatternType, 0)
	for _, p := range w.Patterns {
		if _, ok := types[p.Type]; !ok {
			types[p.Type] = &LicenseTextsGroupedByPatternType{
				Type:   p.Type,
				Map:    make(map[string]*ProjectTextMap, 0),
				Sorted: make([]*ProjectTextMap, 0),
			}
		}
		t := types[p.Type]

		// Then, insert license texts into the map using the hash
		// of the license text, which combines exact matches.
		for _, m := range p.Matches {
			if _, ok := t.Map[m.Hash()]; !ok {
				t.Map[m.Hash()] = &ProjectTextMap{
					LicenseText:        m.Data,
					ProjectNames:       make(map[string]bool, 0),
					FileNames:          make(map[string]bool, 0),
					SortedProjectNames: make([]string, 0),
					SortedFileNames:    make([]string, 0),
				}
			}
			tmap := t.Map[m.Hash()]
			tmap.FileNames[m.FilePath] = true
			pname := fileMap[m.FilePath]
			tmap.ProjectNames[pname] = true
		}
	}

	// Finally, sort everything so the results are deterministic.
	typesList := make([]*LicenseTextsGroupedByPatternType, 0)
	for _, t := range types {
		typesList = append(typesList, t)
		for _, ptm := range t.Map {
			t.Sorted = append(t.Sorted, ptm)

			for fn := range ptm.FileNames {
				ptm.SortedFileNames = append(ptm.SortedFileNames, fn)
			}
			sort.Strings(ptm.SortedFileNames)

			for pn := range ptm.ProjectNames {
				ptm.SortedProjectNames = append(ptm.SortedProjectNames, pn)
			}
			sort.Strings(ptm.SortedProjectNames)
		}
		sort.Slice(t.Sorted, func(i, j int) bool {
			return string(t.Sorted[i].LicenseText) < string(t.Sorted[j].LicenseText)
		})
	}
	sort.Slice(typesList, func(i, j int) bool {
		return typesList[i].Type < typesList[j].Type
	})
	return typesList
}
