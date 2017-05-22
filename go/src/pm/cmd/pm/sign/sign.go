// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package sign implements the `pm sign` command and signs the metadata of a
// package
package sign

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"

	"golang.org/x/crypto/ed25519"
)

// Run creates a pubkey and signature file in the meta directory of the given
// package, using the given private key. The generated signature is computed
// using EdDSA, and includes as a message all files from meta except for any
// pre-existing signature. The resulting signature is written to
// packageDir/meta/signature.
func Run(packageDir string, privateKey ed25519.PrivateKey) error {

	signatureFile := filepath.Join(packageDir, "meta", "signature")

	if err := ioutil.WriteFile(filepath.Join(packageDir, "meta", "pubkey"), privateKey.Public().(ed25519.PublicKey), os.ModePerm); err != nil {
		return err
	}

	// NOTE: cannot use pkg.WalkContents as it is critical that the contents file
	// is signed. It is also important that we establish a deterministic order for
	// the signature.
	metas, err := filepath.Glob(filepath.Join(packageDir, "meta", "*"))
	if err != nil {
		return err
	}
	sort.Strings(metas)
	metaFiles := []string{}
	for _, path := range metas {
		if path == signatureFile {
			continue
		}
		metaFiles = append(metaFiles, path)
	}

	var msg []byte
	for _, f := range metaFiles {
		buf, err := ioutil.ReadFile(f)
		if err != nil {
			return err
		}
		msg = append(msg, buf...)
	}

	sig := ed25519.Sign(privateKey, msg)

	if err := ioutil.WriteFile(signatureFile, sig, os.ModePerm); err != nil {
		return err
	}

	return nil
}
