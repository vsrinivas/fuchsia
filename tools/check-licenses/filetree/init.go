// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

import (
	"encoding/json"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

var AllFileTrees map[string]*FileTree
var RootFileTree *FileTree

func init() {
	AllFileTrees = make(map[string]*FileTree, 0)
}

func Initialize(c *FileTreeConfig) error {
	// Project readme directories should always be skipped.
	// They are processed during the project Initialize() call.
	readmeSkips := &Skip{
		Paths: []string{},
		Notes: []string{"Always skip project.Readmes paths."},
	}
	for _, r := range project.Config.Readmes {
		readmeSkips.Paths = append(readmeSkips.Paths, r.Paths...)
	}
	c.Skips = append(c.Skips, readmeSkips)

	// check-licenses License pattern directories should also be skipped.
	patternSkips := &Skip{
		Paths: []string{},
		Notes: []string{"Always skip license.PatternRoot paths."},
	}
	for _, r := range license.Config.PatternRoots {
		patternSkips.Paths = append(patternSkips.Paths, r.Paths...)
	}
	c.Skips = append(c.Skips, patternSkips)

	// Skip paths are relative to the root fuchsia directory.
	for _, skip := range c.Skips {
		if !skip.SkipAnywhere {
			for i, path := range skip.Paths {
				skip.Paths[i] = filepath.Join(c.FuchsiaDir, path)
			}
		}
	}

	// Save the config file to the out directory (if defined).
	if b, err := json.MarshalIndent(c, "", "  "); err != nil {
		return err
	} else {
		plusFile("_config.json", b)
	}

	Config = c
	return nil
}
