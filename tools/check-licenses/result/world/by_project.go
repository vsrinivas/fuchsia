// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"sort"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// TODO(fxb/102344): Organize the license texts based on the project they were found in.
//
// This is the default mode and can be a simple aggregation of all license
// texts. But that strategy for fuchsia.git can result in an extremely large
// resulting NOTICE file. This file lets us still do some small amount of
// de-duplication on duplicate license texts within the same project.
type ByProject struct {
	Root          string
	Name          string
	SearchResults []*SearchResult

	// Map of ProjectMap structs.
	// This map is keyed on the hash of the license text.
	Map map[string]*SearchResult
}

func NewByProject(p *project.Project) *ByProject {
	return &ByProject{
		Root:          p.Root,
		Name:          p.Name,
		SearchResults: make([]*SearchResult, 0),
		Map:           make(map[string]*SearchResult, 0),
	}
}

type SearchResult struct {
	Data         []byte
	LicenseDatas []*file.FileData
	Pattern      *license.Pattern
}

func NewSearchResult(sr *license.SearchResult) *SearchResult {
	return &SearchResult{
		Data:         sr.LicenseData.Data,
		LicenseDatas: []*file.FileData{sr.LicenseData},
		Pattern:      sr.Pattern,
	}
}

func (sr *SearchResult) AddSearchResult(other *license.SearchResult) {
	sr.LicenseDatas = append(sr.LicenseDatas, other.LicenseData)
}

func (w *World) GroupByProject() []*ByProject {
	byProjects := make([]*ByProject, 0)
	for _, p := range w.FilteredProjects {
		bp := NewByProject(p)
		for _, sr := range p.SearchResults {
			hash := sr.LicenseData.Hash()
			if _, ok := bp.Map[hash]; !ok {
				bp.Map[hash] = NewSearchResult(sr)
			} else {
				bp.Map[hash].AddSearchResult(sr)
			}
		}
		for _, v := range bp.Map {
			bp.SearchResults = append(bp.SearchResults, v)
		}
		sort.Slice(bp.SearchResults, func(i, j int) bool {
			return string(bp.SearchResults[i].Data) < string(bp.SearchResults[j].Data)
		})
		if len(bp.SearchResults) > 0 {
			byProjects = append(byProjects, bp)
		}
	}
	sort.Slice(byProjects, func(i, j int) bool {
		return byProjects[i].Name < byProjects[j].Name
	})
	return byProjects
}
