// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package keys

import (
	"bytes"
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
	randSource = zeroReader{}

	_, exKey, err := ed25519.GenerateKey(randSource)
	if err != nil {
		t.Fatal(err)
	}

	buf := bytes.NewBuffer(nil)

	if err := Gen(buf); err != nil {
		t.Fatal(err)
	}

	key := buf.Bytes()

	if !bytes.Equal(key, exKey) {
		t.Errorf("key: got %x, want %x", key, exKey)
	}
}
