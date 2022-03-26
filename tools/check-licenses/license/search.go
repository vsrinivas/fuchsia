// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"context"
	"fmt"

	"golang.org/x/sync/errgroup"

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

	eg, _ := errgroup.WithContext(context.Background())

	// Run the license patterns on the parsed license texts.
	// Save the match results to a result object.
	for _, d := range f.Data {
		matches := make([]bool, len(patterns))
		for i, p := range patterns {
			// https://golang.org/doc/faq#closures_and_goroutines
			i := i
			p := p
			eg.Go(func() error {
				matches[i] = p.Search(d)
				return nil
			})

			// The license patterns have a lot of overlap right now.
			// A single license segment may match to 5 or more different patterns.
			// This results in really large output files, since some of the texts
			// are duplicated (when grouping by patterns).
			//
			// TODO: Clean up the license texts to make this not happen.
			// In the mean time, break after the first match.
			if err := eg.Wait(); err != nil {
				return nil, err
			}

			if matches[i] {
				break
			}
		}

		matchFound := false
		for i := range matches {
			p := patterns[i]
			if matches[i] {
				result := &SearchResult{
					LicenseData: d,
					Pattern:     p,
				}
				AllSearchResults = append(AllSearchResults, result)
				searchResults = append(searchResults, result)
				matchFound = true

				// The "unrecognized" license pattern is put at the end of the list.
				// If it is the only license that matched the text, that's bad.
				// Record these instances in the metrics, so we can investigate later.
				if patterns[i].Name == Unrecognized.Name {
					plusVal(UnrecognizedLicenses, fmt.Sprintf("%v - %v", f.Path, d.LibraryName))
				}
			}
		}
		if !matchFound {
			plusVal(UnrecognizedLicenses, fmt.Sprintf("%v - %v", f.Path, d.LibraryName))
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
			plusVal(NumUnusedPatterns, p.Name)
		}
	}
}
