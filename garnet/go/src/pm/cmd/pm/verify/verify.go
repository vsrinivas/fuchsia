// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package verify implements the `pm verify` command
package verify

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
)

const usage = `Usage: %s verify
verify metadata signature against the embedded public key
`

// Run ensures that packageDir/meta/signature is a valid EdDSA signature of
// meta/* by the public key in meta/pubkey
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("verify", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	return build.Verify(cfg)
}
