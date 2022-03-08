// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"os"
	"path/filepath"
)

func Initialize(c *LicenseConfig) error {
	AllPatterns = make([]*Pattern, 0)
	AllSearchResults = make(map[string]*SearchResult, 0)

	// Initialize all license patterns.
	for _, pr := range c.PatternRoots {
		for _, root := range pr.Paths {
			if err := filepath.Walk(root, patternsWalker); err != nil {
				return err
			}
		}
	}

	Config = c
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
