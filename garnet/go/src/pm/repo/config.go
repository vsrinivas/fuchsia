// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repo

import (
	"flag"
	"os"
	"path/filepath"
	"runtime"
)

func platformDataDir() string {
	switch runtime.GOOS {
	case "darwin":
		return filepath.Join(os.Getenv("HOME"), "Library", "Application Support", "Fuchsia")

	case "windows":
		return filepath.Join(os.Getenv("APPDATA"), "Fuchsia")

	default:
		d := os.Getenv("XDG_DATA_HOME")
		if d == "" {
			d = filepath.Join(os.Getenv("HOME"), ".local", "share")
		}
		return filepath.Join(d, "Fuchsia")

	}
}

// Config contains a common runtime configuration for repository manipulations.
type Config struct {
	RepoDir       string
	TimeVersioned bool
}

func (c *Config) Vars(fs *flag.FlagSet) {
	fs.StringVar(&c.RepoDir, "repo", "", "path to repostory directory")
	fs.BoolVar(&c.TimeVersioned, "vt", false, "Set repo versioning based on time rather than a monotonic increment")
}

func (c *Config) ApplyDefaults() {
	if c.RepoDir != "" {
		return
	}

	buildDir := os.Getenv("FUCHSIA_BUILD_DIR")
	if buildDir != "" {
		if c.RepoDir == "" {
			c.RepoDir = filepath.Join(buildDir, "amber-files")
		}
	} else {
		if c.RepoDir == "" {
			c.RepoDir = filepath.Join(platformDataDir(), "amber-files")
		}
	}
}
