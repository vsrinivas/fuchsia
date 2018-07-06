// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package genkey is the `pm genkey` command, and generates Ed25519 package signing keys
package genkey

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/keys"
)

const usage = `Usage: %s genkey
generate a new private key
`

// Run generates a new keypair and writes it to `key` in binary format.
// The generated keys are suitable for use with EdDSA, specifically for `pm
// sign`
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

	if cfg.KeyPath == "" {
		return fmt.Errorf("error: signing key flag is required")
	}
	f, err := os.Create(cfg.KeyPath)
	if err != nil {
		return err
	}
	defer f.Close()
	return keys.Gen(f)
}
