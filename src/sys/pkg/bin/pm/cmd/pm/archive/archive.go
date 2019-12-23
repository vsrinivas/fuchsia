// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package archive implements the `pm archive` command
package archive

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
)

const usage = `Usage: %s archive
construct a single .far representation of the package
`

// Run reads the configured package meta FAR and produces a whole-package
// archive including the metadata and the blobs.
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("archive", flag.ExitOnError)

	var output = fs.String("output", "", "Archive output path. `.far` will be appended.")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	return build.Archive(cfg, *output)
}
