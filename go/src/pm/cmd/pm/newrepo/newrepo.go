// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package newrepo contains the `pm newrepo` command
package newrepo

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/publish"
	"fuchsia.googlesource.com/pm/repo"
)

const usage = `Usage: %s newrepo
create a new repostory and associated key material
`

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("newrepo", flag.ExitOnError)

	config := &repo.Config{}
	config.Vars(fs)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}
	config.ApplyDefaults()

	os.MkdirAll(config.RepoDir, os.ModePerm)
	os.MkdirAll(config.KeyDir, os.ModePerm)

	_, err := publish.InitRepo(config.RepoDir, config.KeyDir)

	return err
}
