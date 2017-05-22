// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package keys

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"golang.org/x/crypto/ed25519"
)

type zeroReader struct{}

func (z zeroReader) Read(buf []byte) (int, error) {
	for i := range buf {
		buf[i] = 0
	}
	return len(buf), nil
}

func TestGen(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(d)

	randSource = zeroReader{}

	exPub, exKey, err := ed25519.GenerateKey(randSource)
	if err != nil {
		t.Fatal(err)
	}

	if err := Gen(d); err != nil {
		t.Fatal(err)
	}

	privKey, err := ioutil.ReadFile(filepath.Join(d, "key"))
	if err != nil {
		t.Fatal(err)
	}
	pubKey, err := ioutil.ReadFile(filepath.Join(d, "pub"))
	if err != nil {
		t.Fatal(err)
	}

	if !bytes.Equal(privKey, exKey) {
		t.Errorf("private key: got %x, want %x", privKey, exKey)
	}

	if !bytes.Equal(pubKey, exPub) {
		t.Errorf("public key: got %x, want %x", privKey, exKey)
	}
}
