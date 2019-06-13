// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package seal implements the `pm seal` command
package seal

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
)

const usage = `Usage: %s seal
seal package metadata into a meta.far
`

// Run archives the meta/ directory into meta.far.
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("seal", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	_, err := build.Seal(cfg)
	return err
}
