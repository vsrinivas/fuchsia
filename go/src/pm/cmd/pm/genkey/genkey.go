// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package genkey is the `pm genkey` command, and generates Ed25519 package signing keys
package genkey

import (
	"fuchsia.googlesource.com/pm/keys"
)

// Run generates a new private/public keypair. The keys are written to
// outdir/key and outdir/pub in binary format. The generated keys are suitable
// for use with EdDSA.
func Run(outdir string) error {
	return keys.Gen(outdir)
}
