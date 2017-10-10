// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package verify implements the `pm verify` command
package verify

import (
	"fuchsia.googlesource.com/pm/build"
)

// Run ensures that packageDir/meta/signature is a valid EdDSA signature of
// meta/* by the public key in meta/pubkey
func Run(cfg *build.Config) error {
	return build.Verify(cfg)
}
