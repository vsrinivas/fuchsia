// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package keys provides utilities for generating package manager signing keys
package keys

import (
	"crypto/rand"
	"io"

	"golang.org/x/crypto/ed25519"
)

// tests override this for deterministic output
var randSource = rand.Reader

// Gen generates a new keypair and writes the private key to the given writer.
// The generated key contains a private and public key suitable for use with EdDSA.
func Gen(w io.Writer) error {
	_, key, err := ed25519.GenerateKey(randSource)
	if err != nil {
		return err
	}

	_, err = w.Write(key)
	return err
}
