// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package genkey is the `pm genkey` command, and generates Ed25519 package signing keys
package genkey

import (
	"crypto/rand"
	"os"
	"path/filepath"

	"io/ioutil"

	"golang.org/x/crypto/ed25519"
)

// tests override this for deterministic output
var randSource = rand.Reader

// Run generates a new private/public keypair. The keys are written to
// outdir/key and outdir/pub in binary format. The generated keys are suitable
// for use with EdDSA.
func Run(outdir string) error {
	pubKey, privKey, err := ed25519.GenerateKey(randSource)
	if err != nil {
		return err
	}

	if err := ioutil.WriteFile(filepath.Join(outdir, "key"), privKey, os.ModePerm); err != nil {
		return err
	}
	return ioutil.WriteFile(filepath.Join(outdir, "pub"), pubKey, os.ModePerm)
}
