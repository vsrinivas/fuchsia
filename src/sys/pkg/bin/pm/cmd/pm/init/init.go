// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package init contains the `pm init` command
package init

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
)

const usage = `Usage: %s init
initialize a package meta directory in the standard form
`

// Run initializes package metadata in the given package directory. A manifest
// is generated with a name matching the directory name. A content manifest is
// also created including all files found in the package directory.
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("init", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	return build.Init(cfg)
}
