// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"os"
	"path/filepath"
	"regexp"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
)

var (
	AllPatterns          []*Pattern
	AllCopyrightPatterns []*Pattern
	AllSearchResults     map[string]*SearchResult

	unrecognized *Pattern
)

func init() {
	AllPatterns = make([]*Pattern, 0)
	AllCopyrightPatterns = make([]*Pattern, 0)
	AllSearchResults = make(map[string]*SearchResult, 0)
}

func Initialize(c *LicenseConfig) error {
	// Initialize all license patterns.
	for _, pr := range c.PatternRoots {
		for _, root := range pr.Paths {
			if err := filepath.Walk(root, patternsWalker); err != nil {
				return err
			}
		}
	}

	Config = c

	// Unrecognized license texts won't be added to the resulting NOTICE file.
	// This is good behavior, all texts should be recognized. But until we can add
	// all the necessary license patterns, add all unrecognized texts to a catch-all
	// pattern.
	re, err := regexp.Compile("(?P<text>.*)")
	if err != nil {
		return err
	}
	unrecognized = &Pattern{
		Name:               "_unrecognized",
		Matches:            make([]*file.FileData, 0),
		previousMatches:    make(map[string]bool),
		previousMismatches: make(map[string]bool),
		re:                 re,
	}
	// TODO(jcecil): Remove this pattern from AllPatterns if/when
	// we re-enable searching with all available patterns.
	AllPatterns = append(AllPatterns, unrecognized)
	return nil
}

func patternsWalker(path string, info os.FileInfo, err error) error {
	if info.IsDir() {
		return nil
	}
	pattern, err := NewPattern(path)
	if err != nil {
		return err
	}

	plusVal(NumPatterns, path)
	AllPatterns = append(AllPatterns, pattern)
	return nil
}
