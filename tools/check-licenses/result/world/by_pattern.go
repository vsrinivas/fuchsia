// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"sort"
	"strings"
)

// Group the license texts based on the patterns that they matched.
//
// This allows us to shrink the final notice file down significantly,
// since many projects share the same exact license text.
type ByPattern struct {
	// Pattern type (e.g. BSD, MIT).
	PatternType string

	// Map of ProjectMap structs.
	// This map is keyed on the hash of the license text.
	Map map[string]*ProjectMap

	// Sorted list to produce a deterministic result.
	Sorted []*ProjectMap
}

func NewByPattern(pattern string) *ByPattern {
	return &ByPattern{
		PatternType: pattern,
		Map:         make(map[string]*ProjectMap, 0),
		Sorted:      make([]*ProjectMap, 0),
	}
}

type ProjectMap struct {
	// License text content.
	LicenseText []byte

	// Project names that this license text appears in.
	Projects map[string]bool

	// File paths that contain this license text.
	Paths map[string]bool

	// Sorted lists to produce deterministic results.
	SortedNames []string
	SortedPaths []string
}

func (pm *ProjectMap) LicenseTextHTML() string {
	result := string(pm.LicenseText)
	result = strings.ReplaceAll(result, "<", "&lt;")
	result = strings.ReplaceAll(result, ">", "&gt;")
	result = strings.ReplaceAll(result, "&", "&amp;")
	result = strings.ReplaceAll(result, `"`, "&quot;")
	return result
}

func NewProjectMap(data []byte) *ProjectMap {
	return &ProjectMap{
		LicenseText: data,
		Projects:    make(map[string]bool, 0),
		Paths:       make(map[string]bool, 0),
		SortedNames: make([]string, 0),
		SortedPaths: make([]string, 0),
	}
}

func (w *World) GroupByPattern() []*ByPattern {
	fileMap := w.getFilePathToProjectMap()

	// Start by enumerating all of the different license pattern types
	// (BSD, MIT, etc) that we use in this run
	byPatternTypeMap := make(map[string]*ByPattern, 0)
	for _, p := range w.Patterns {
		if _, ok := byPatternTypeMap[p.Type]; !ok {
			byPatternTypeMap[p.Type] = NewByPattern(p.Type)
		}
		bpt := byPatternTypeMap[p.Type]

		// Then, insert license texts into the map using the hash
		// of the license text, which combines exact matches.
		for _, m := range p.Matches {
			hash := m.Hash()
			path := m.FilePath
			project, ok := fileMap[path]
			if !ok {
				continue
			}

			if _, ok := bpt.Map[hash]; !ok {
				bpt.Map[hash] = NewProjectMap(m.Data)
			}

			bpt.Map[hash].Paths[path] = true
			bpt.Map[hash].Projects[project] = true
		}
	}

	// Finally, sort everything so the results are deterministic.
	byPatternList := make([]*ByPattern, 0)
	for _, bpt := range byPatternTypeMap {
		byPatternList = append(byPatternList, bpt)
		for _, projectMap := range bpt.Map {
			bpt.Sorted = append(bpt.Sorted, projectMap)

			for path := range projectMap.Paths {
				projectMap.SortedPaths = append(projectMap.SortedPaths, path)
			}
			sort.Strings(projectMap.SortedPaths)

			for name := range projectMap.Projects {
				projectMap.SortedNames = append(projectMap.SortedNames, name)
			}
			sort.Strings(projectMap.SortedNames)
		}
		sort.Slice(bpt.Sorted, func(i, j int) bool {
			return string(bpt.Sorted[i].LicenseText) < string(bpt.Sorted[j].LicenseText)
		})
	}
	sort.Slice(byPatternList, func(i, j int) bool {
		return byPatternList[i].PatternType < byPatternList[j].PatternType
	})
	return byPatternList
}

func (w *World) getFilePathToProjectMap() map[string]string {
	fileMap := make(map[string]string, 0)
	for _, p := range w.FilteredProjects {
		for _, f := range p.Files {
			fileMap[f.AbsPath] = p.Name
		}
		for _, f := range p.LicenseFile {
			fileMap[f.AbsPath] = p.Name
		}
	}
	return fileMap
}
