// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"crypto/x509"
	"encoding/pem"
	"io/ioutil"
	"os"
	"testing"

	"golang.org/x/crypto/ssh"
)

func TestGeneratePrivateKey(t *testing.T) {
	path, err := generatePrivateKey()
	if err != nil {
		t.Fatalf("generatePrivateKey() failed: got %s, want <nil> error", err)
	}
	defer os.Remove(path)

	// Load the pkey and ensure that it's in the right format.
	b, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatalf("ReadFile(%s) failed: got %s, want <nil> error", path, err)
	}
	block, _ := pem.Decode(b)
	pkey, err := x509.ParsePKCS1PrivateKey(block.Bytes)
	if err != nil {
		t.Fatalf("x509.ParsePKCS1PrivateKey(bytes) failed: got %s, want <nil> error", err)
	}
	if err := pkey.Validate(); err != nil {
		t.Errorf("pkey.Validate() failed: got %s, want <nil> error", err)
	}
}

func TestGeneratePublicKey(t *testing.T) {
	pkeyPath, err := generatePrivateKey()
	if err != nil {
		t.Fatalf("generatePrivateKey() failed: got %s, want <nil> error", err)
	}
	defer os.Remove(pkeyPath)
	path, err := generatePublicKey(pkeyPath)
	if err != nil {
		t.Fatalf("generatePublicKey(%s) failed: got %s, want <nil> error", pkeyPath, err)
	}
	defer os.Remove(path)

	// Load the public key and ensure that it's in the right format.
	b, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatalf("ReadFile(%s) failed: got %s, want <nil> error", path, err)
	}
	key, _, _, _, err := ssh.ParseAuthorizedKey(b)
	if err != nil {
		t.Fatalf("ssh.ParseAuthorizedKey(pubkeyBytes) failed: got %s, want <nil> error", err)
	}
	if key.Type() != "ssh-rsa" {
		t.Errorf("key %s has wrong type: got %s, want ssh-rsa", key, key.Type())
	}
}
