// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"crypto/ed25519"
	"crypto/x509"
	"encoding/base64"
	"encoding/pem"
	"io/ioutil"
	"path/filepath"
	"reflect"
	"testing"
)

func TestSign(t *testing.T) {
	dir := t.TempDir()
	var pkey ed25519.PrivateKey
	dataFile := filepath.Join(dir, "data")

	uploads := []Upload{{
		Source:      dataFile,
		Destination: "data",
	}}
	actual, err := Sign(uploads, pkey)
	if err != nil {
		t.Errorf("failed to sign uploads: %v", err)
	}
	if !reflect.DeepEqual(actual, uploads) {
		t.Errorf("missing pkey should return unmodified uploads; got %v", actual)
	}

	_, pkey, err = ed25519.GenerateKey(nil)
	if err != nil {
		t.Errorf("failed to generate key: %v", err)
	}
	actual, err = Sign(uploads, pkey)
	if err != nil {
		t.Errorf("failed to sign uploads: %v", err)
	}
	if !reflect.DeepEqual(actual, uploads) {
		t.Errorf("missing data file should return unmodified uploads; got %v", actual)
	}

	err = ioutil.WriteFile(dataFile, []byte("data"), 0o400)
	if err != nil {
		t.Errorf("failed to write data file: %v", err)
	}
	expectedSignature := base64.StdEncoding.EncodeToString(ed25519.Sign(pkey, []byte("data")))
	expected := []Upload{{
		Source:      dataFile,
		Destination: "data",
		Metadata: map[string]string{
			signatureKey: expectedSignature,
		},
	}}
	actual, err = Sign(uploads, pkey)
	if err != nil {
		t.Errorf("failed to sign uploads: %v", err)
	}
	if !reflect.DeepEqual(actual, expected) {
		t.Errorf("expected: %v, actual: %v", expected, actual)
	}
}

func TestPublicKeyUpload(t *testing.T) {
	upload, err := PublicKeyUpload("namespace", []byte{})
	if err == nil {
		t.Errorf("nil public key should err")
	}
	if upload != nil {
		t.Errorf("nil public key should return nil pubkey upload; got: %v", upload)
	}

	expectedPubkey, pkey, err := ed25519.GenerateKey(nil)
	if err != nil {
		t.Errorf("failed to generate key: %v", err)
	}

	upload, err = PublicKeyUpload("namespace", pkey.Public().(ed25519.PublicKey))
	if err != nil {
		t.Errorf("failed to derive public key: %v", err)
	}
	if upload == nil || len(upload.Contents) == 0 {
		t.Errorf("got empty pubkey data")
	}
	expectedDest := filepath.Join("namespace", releasePubkeyFilename)
	if upload.Destination != expectedDest {
		t.Errorf("incorrect destination; got: %s, expected: %s", upload.Destination, expectedDest)
	}
	block, _ := pem.Decode(upload.Contents)
	if block.Bytes == nil {
		t.Errorf("failed to decode public key from pem")
	}
	pubkey, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		t.Errorf("failed to parse public key from DER bytes")
	}
	if string(pubkey.(ed25519.PublicKey)) != string(expectedPubkey) {
		t.Errorf("got: %s, expected: %s", string(pubkey.(ed25519.PublicKey)), string(expectedPubkey))
	}
}
