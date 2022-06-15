// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"fmt"

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
					plusVal(UnrecognizedLicenses, fmt.Sprintf("%v - %v", f.Path, d.LibraryName))
				}

				break
			}
		}
	}
	return searchResults, nil
}

// If a license pattern goes unused, it means that license pattern is no longer needed.
// We should try to consolidate the license patterns down to the smallest necessary set.
// Record these patterns here, so we can improve the tool.
func RecordUnusedPatterns() {
	for _, p := range AllPatterns {
		if len(p.Matches) == 0 {
			plusVal(NumUnusedPatterns, fmt.Sprintf("%v-%v-%v", p.Category, p.Type, p.Name))
		}
	}
}
