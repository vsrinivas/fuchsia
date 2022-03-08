// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"fmt"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license/notice"
)

type SearchResult struct {
	Patterns []*Pattern
}

var AllPatterns []*Pattern
var AllSearchResults map[string]*SearchResult

// Search the given data slice for text that matches this License pattern.
func Search(f *file.File) (*SearchResult, error) {

	// First, make sure we haven't already seen this license text before.
	hash, err := f.Hash()
	if err != nil {
		return nil, err
	}
	if result, ok := AllSearchResults[hash]; ok {
		// We've seen this before. Return the previous SearchResult.
		plusVal(fmt.Sprintf("%v [%v]", DuplicateLicenseText, hash), f.Path)
		return result, nil
	}

	// Parse the files into a structured data format.
	data := make([]*notice.Data, 0)

	// The "LicenseFormat" field of each file is set at the project level
	// (in README.fuchsia files) and it affects how they are analyzed here.
	switch f.LicenseFormat {

	// Default: File.LicenseFormat == Any
	// This is most likely a regular source file in the repository.
	// May or may not have copyright information.
	//
	// TODO(jcecil): Consider using a smaller set of patterns on these files
	// to help it run faster.
	case file.Any:
		text, err := f.ReadAll()
		if err != nil {
			return nil, err
		}
		data = append(data, &notice.Data{
			LicenseText: text,
		})

	// File.LicenseFormat == CopyrightHeader
	// All source files belonging to "The Fuchsia Authors" (fuchsia.git)
	// must contain Copyright header information.
	case file.CopyrightHeader:
		text, err := f.ReadAll()
		if err != nil {
			return nil, err
		}
		data = append(data, &notice.Data{
			LicenseText: text,
		})

	// File.LicenseFormat == SingleLicense
	// Regular LICENSE files that contain text for a single license.
	case file.SingleLicense:
		text, err := f.ReadAll()
		if err != nil {
			return nil, err
		}
		data = append(data, &notice.Data{
			LicenseText: text,
		})

	// File.LicenseFormat == MultiLicense*
	// NOTICE files that contain text for multiple licenses.
	// See the files in the /notice subdirectory for more info.
	case file.MultiLicenseChromium:
		data, err = notice.ParseChromium(f)
		if err != nil {
			return nil, err
		}
	case file.MultiLicenseFlutter:
		data, err = notice.ParseFlutter(f)
		if err != nil {
			return nil, err
		}
	case file.MultiLicenseGoogle:
		data, err = notice.ParseGoogle(f)
		if err != nil {
			return nil, err
		}
	}

	result := SearchResult{
		Patterns: make([]*Pattern, 0),
	}

	// Run the license patterns on the parsed license texts.
	// Save the match results to a result object.
	var wg sync.WaitGroup
	for _, d := range data {
		matches := make([]bool, len(AllPatterns))
		for i, p := range AllPatterns {
			wg.Add(1)
			go func(i int, p *Pattern, text []byte) {
				defer wg.Done()
				matches[i] = p.Search([]byte(d.LicenseText))
			}(i, p, d.LicenseText)
		}
		wg.Wait()

		matchFound := false
		for i := range matches {
			if matches[i] {
				result.Patterns = append(result.Patterns, AllPatterns[i])
				matchFound = true
			}
		}
		if !matchFound {
			plusVal(UnrecognizedLicenses, fmt.Sprintf("%v - %v", f.Path, d.LibraryName))
		}
	}

	AllSearchResults[hash] = &result
	return &result, nil
}
