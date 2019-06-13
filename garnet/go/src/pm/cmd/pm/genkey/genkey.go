// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package genkey is deprecated
package genkey

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
)

const usage = `Usage: %s genkey
deprecated without replacement
`

// Run performs a null action due to deprecation
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("genkey", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	fmt.Fprintln(os.Stderr, "package signing is deprecated")

	if cfg.KeyPath == "" {
		return fmt.Errorf("error: signing key flag is required")
	}

	f, err := os.Create(cfg.KeyPath)
	if err != nil {
		return err
	}
	defer f.Close()
	return nil
}
