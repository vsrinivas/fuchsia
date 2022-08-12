// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"fmt"
	"path/filepath"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
)

type SearchResult struct {
	LicenseData *file.FileData
	Pattern     *Pattern
}

// Search each data slice in the given file for license texts.
// f (file) is assumed to be a single or multi license file, where all content
// in the file is license information.
func Search(f *file.File) ([]*SearchResult, error) {
	return search(f, AllPatterns)
}

// SearchHeaders searches the beginning portion of the given file for
// copyright text.
// f (file) is assumed to be some source artifact, which may contain
// a small section of licensing or copyright information at the top of the file.
func SearchHeaders(f *file.File) ([]*SearchResult, error) {
	return search(f, AllCopyrightPatterns)
}

// search the file using the given list of patterns.
func search(f *file.File, patterns []*Pattern) ([]*SearchResult, error) {
	searchResults := make([]*SearchResult, 0)

	// Run the license patterns on the parsed license texts.
	// Save the match results to a result object.
	for _, d := range f.Data {
		for _, p := range patterns {
			if p.Search(d) {
				result := &SearchResult{
					LicenseData: d,
					Pattern:     p,
				}
				AllSearchResults = append(AllSearchResults, result)
				searchResults = append(searchResults, result)

				// The "unrecognized" license pattern is put at the end of the list.
				// If it is the only license that matched the text, that's bad.
				// Record these instances in the metrics, so we can investigate later.
				if p.Name == Unrecognized.Name {
					plusVal(UnrecognizedLicenses, fmt.Sprintf("%v - %v", f.RelPath, d.LibraryName))
				}

				break
			}
		}
	}
	if len(searchResults) > 0 {
		base := filepath.Base(f.RelPath)
		path := filepath.Join("matches", f.RelPath, base)
		plusFile(path, f.Text)
		for iter, r := range searchResults {
			dir := filepath.Dir(path)
			segPath := filepath.Join(dir, "segments", strconv.Itoa(iter))
			plusFile(segPath, r.LicenseData.Data)
		}
	}
	return searchResults, nil
}

// Perform any cleanup steps after the license search has completed.
func Finalize() {

	// If a license pattern goes unused, it means that license pattern is no longer needed.
	// We should try to consolidate the license patterns down to the smallest necessary set.
	// Record these patterns here, so we can improve the tool.
	for _, p := range AllPatterns {
		if len(p.Matches) == 0 {
			plusVal(NumUnusedPatterns, fmt.Sprintf("%v-%v-%v", p.Category, p.Type, p.Name))
		}
	}

	// Record matches in the metrics directory.
	for _, p := range AllPatterns {
		if len(p.Matches) == 0 {
			continue
		}

		var m strings.Builder
		for _, match := range p.Matches {
			m.WriteString(match.FilePath)
			m.WriteString("\n")
		}
		plusFile(filepath.Join("patterns", p.RelPath, "matches"), []byte(m.String()))
	}
}
