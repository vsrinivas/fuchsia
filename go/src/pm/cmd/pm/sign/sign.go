// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package sign implements the `pm sign` command and signs the metadata of a
// package
package sign

import (
	"fuchsia.googlesource.com/pm/pkg"
	"golang.org/x/crypto/ed25519"
)

// Run creates a pubkey and signature file in the meta directory of the given
// package, using the given private key. The generated signature is computed
// using EdDSA, and includes as a message all files from meta except for any
// pre-existing signature. The resulting signature is written to
// packageDir/meta/signature.
func Run(packageDir string, privateKey ed25519.PrivateKey) error {
	return pkg.Sign(packageDir, privateKey)
}
