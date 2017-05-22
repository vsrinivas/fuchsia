// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package genkey is the `pm genkey` command, and generates Ed25519 package signing keys
package genkey

import (
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/keys"
)

// Run generates a new keypair and writes it to outdir/key in binary format.
// The generated keys are suitable for use with EdDSA, specifically for `pm
// sign`
func Run(outdir string) error {
	f, err := os.Create(filepath.Join(outdir, "key"))
	if err != nil {
		return err
	}
	defer f.Close()
	return keys.Gen(f)
}
