// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package keys provides utilities for generating package manager signing keys
package keys

import (
	"crypto/rand"
	"io/ioutil"
	"os"
	"path/filepath"

	"golang.org/x/crypto/ed25519"
)

// tests override this for deterministic output
var randSource = rand.Reader

// Gen generates a new private/public keypair. The keys are written to
// outdir/key and outdir/pub in binary format. The generated keys are suitable
// for use with EdDSA.
func Gen(outdir string) error {
	pubKey, privKey, err := ed25519.GenerateKey(randSource)
	if err != nil {
		return err
	}

	if err := ioutil.WriteFile(filepath.Join(outdir, "key"), privKey, os.ModePerm); err != nil {
		return err
	}
	return ioutil.WriteFile(filepath.Join(outdir, "pub"), pubKey, os.ModePerm)
}
