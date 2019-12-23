// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package ignores implements a basic set of patterns to ignore when
// constructing packages. TODO(raggi): replace with something user configurable.
package ignores

import "path/filepath"

var patterns = []string{
	".git",
	".jiri",
	".hg",
}

// Match returns true if the given path matches an ignored file patttern
func Match(path string) bool {
	path = filepath.Clean(path)
	dir, name := filepath.Dir(path), filepath.Base(path)

	for name != "" && name != "/" && name != "." {
		for _, pat := range patterns {
			matched, err := filepath.Match(pat, name)
			if err != nil {
				panic("pm: ignores: bad pattern: " + pat)
			}
			if matched {
				return matched
			}
		}

		dir, name = filepath.Dir(dir), filepath.Base(dir)
	}
	return false
}
