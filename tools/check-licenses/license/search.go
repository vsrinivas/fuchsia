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
	Patterns    []*Pattern
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

		result := &SearchResult{
			LicenseData: d,
			Patterns:    make([]*Pattern, 0),
		}

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
			if matches[i] {
				result.Patterns = append(result.Patterns, patterns[i])
				matchFound = true
			}
		}
		searchResults = append(searchResults, result)
		if !matchFound {
			plusVal(UnrecognizedLicenses, fmt.Sprintf("%v - %v", f.Path, d.LibraryName))
		}
	}

	return searchResults, nil

}
