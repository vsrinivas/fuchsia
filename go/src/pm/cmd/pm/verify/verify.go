// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package verify implements the `pm verify` command
package verify

import (
	"errors"
	"io/ioutil"
	"path/filepath"
	"sort"

	"golang.org/x/crypto/ed25519"
)

// ErrVerificationFailed indicates that a package failed to verify
var ErrVerificationFailed = errors.New("package verification failed")

// Run ensures that packageDir/meta/signature is a valid EdDSA signature of
// meta/* by the public key in meta/pubkey
func Run(packageDir string) error {

	signatureFile := filepath.Join(packageDir, "meta", "signature")

	pubkey, err := ioutil.ReadFile(filepath.Join(packageDir, "meta", "pubkey"))
	if err != nil {
		return err
	}

	sig, err := ioutil.ReadFile(filepath.Join(packageDir, "meta", "signature"))
	if err != nil {
		return err
	}

	metas, err := filepath.Glob(filepath.Join(packageDir, "meta", "*"))
	if err != nil {
		return err
	}
	sort.Strings(metas)
	var msg []byte
	for _, path := range metas {
		if path == signatureFile {
			continue
		}
		buf, err := ioutil.ReadFile(path)
		if err != nil {
			return err
		}

		msg = append(msg, buf...)
	}

	if !ed25519.Verify(pubkey, msg, sig) {
		return ErrVerificationFailed
	}

	return nil
}
