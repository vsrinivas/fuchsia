// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package build contains the `pm build` command
package build

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/seal"
	"fuchsia.googlesource.com/pm/cmd/pm/sign"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
)

const usage = `Usage: %s build
perform update, sign and seal in order
`

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("build", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	if err := update.Run(cfg, []string{}); err != nil {
		return err
	}

	if err := sign.Run(cfg, []string{}); err != nil {
		return err
	}

	if err := seal.Run(cfg, []string{}); err != nil {
		return err
	}

	return nil
}
