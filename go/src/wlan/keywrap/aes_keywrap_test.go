// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package keywrap_test

import (
	"bytes"
	"encoding/hex"
	"testing"
	. "wlan/keywrap"
)

// RFC 3394, 4.1 Wrap 128 bits of Key Data with a 128-bit KEK
func Test128Data_128Kek(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF")

	ciphertext, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}
	expected, _ := hex.DecodeString("1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5")
	if bytes.Compare(ciphertext, expected) != 0 {
		t.Fatal("Wrong ciphertext.")
	}

	plaintext, err := Unwrap(kek, expected)
	if bytes.Compare(plaintext, data) != 0 {
		t.Fatal("Wrong plaintext.")
	}
}

// RFC 3394, 4.2 Wrap 128 bits of Key Data with a 192-bit KEK
func Test128Data_192Kek(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F1011121314151617")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF")

	ciphertext, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}
	expected, _ := hex.DecodeString("96778B25AE6CA435F92B5B97C050AED2468AB8A17AD84E5D")
	if bytes.Compare(ciphertext, expected) != 0 {
		t.Fatal("Wrong ciphertext.")
	}

	plaintext, err := Unwrap(kek, expected)
	if bytes.Compare(plaintext, data) != 0 {
		t.Fatal("Wrong plaintext.")
	}
}

// RFC 3394, 4.3 Wrap 128 bits of Key Data with a 256-bit KEK
func Test128Data_256Kek(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF")

	ciphertext, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}
	expected, _ := hex.DecodeString("64E8C3F9CE0F5BA263E9777905818A2A93C8191E7D6E8AE7")
	if bytes.Compare(ciphertext, expected) != 0 {
		t.Fatal("Wrong ciphertext.")
	}

	plaintext, err := Unwrap(kek, expected)
	if bytes.Compare(plaintext, data) != 0 {
		t.Fatal("Wrong plaintext.")
	}
}

// RFC 3394, 4.4 Wrap 192 bits of Key Data with a 192-bit KEK
func Test192Data_192Kek(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F1011121314151617")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF0001020304050607")

	ciphertext, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}
	expected, _ := hex.DecodeString("031D33264E15D33268F24EC260743EDCE1C6C7DDEE725A936BA814915C6762D2")
	if bytes.Compare(ciphertext, expected) != 0 {
		t.Fatal("Wrong ciphertext.")
	}

	plaintext, err := Unwrap(kek, expected)
	if bytes.Compare(plaintext, data) != 0 {
		t.Fatal("Wrong plaintext.")
	}
}

// RFC 3394, 4.5 Wrap 192 bits of Key Data with a 256-bit KEK
func Test192Data_256Kek(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF0001020304050607")

	ciphertext, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}
	expected, _ := hex.DecodeString("A8F9BC1612C68B3FF6E6F4FBE30E71E4769C8B80A32CB8958CD5D17D6B254DA1")
	if bytes.Compare(ciphertext, expected) != 0 {
		t.Fatal("Wrong ciphertext.")
	}

	plaintext, err := Unwrap(kek, expected)
	if bytes.Compare(plaintext, data) != 0 {
		t.Fatal("Wrong plaintext.")
	}
}

// RFC 3394, 4.6 Wrap 256 bits of Key Data with a 256-bit KEK
func Test256Data_256Kek(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F")

	ciphertext, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}
	expected, _ := hex.DecodeString("28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21")
	if bytes.Compare(ciphertext, expected) != 0 {
		t.Fatal("Wrong ciphertext.")
	}

	plaintext, err := Unwrap(kek, expected)
	if bytes.Compare(plaintext, data) != 0 {
		t.Fatal("Wrong plaintext.")
	}
}

func TestInvalidKeyLength(t *testing.T) {
	kek, _ := hex.DecodeString("0102030405060708090A0B0C0D0E0F") // 240-bit
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF")

	_, err := Wrap(kek, data)
	if err == nil {
		t.Fatal("Expected failure with invalid key length", err)
	}

	_, err = Unwrap(kek, data)
	if err == nil {
		t.Fatal("Expected failure with invalid key length", err)
	}
}

func TestInvalidDataLength(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F")
	data, _ := hex.DecodeString("01234567891234560123456789123456012345678912345601234567891234561")

	_, err := Wrap(kek, data)
	if err == nil {
		t.Fatal("Expected failure with plaintext not being a multiple of 64-bit blocks", err)
	}

	_, err = Unwrap(kek, data)
	if err == nil {
		t.Fatal("Expected failure with ciphertext not being a multiple of 64-bit blocks", err)
	}
}

func TestUnwrapWithWrongKey(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F")
	data, _ := hex.DecodeString("00112233445566778899AABBCCDDEEFF")

	_, err := Wrap(kek, data)
	if err != nil {
		t.Fatal("Wrapping failed with error. ", err)
	}

	kek[0] = 0xFF
	_, err = Unwrap(kek, data)
	if err == nil {
		t.Fatal("Expected failure with wrong key", err)
	}
}

func TestTooShortDataLength(t *testing.T) {
	kek, _ := hex.DecodeString("000102030405060708090A0B0C0D0E0F")
	data, _ := hex.DecodeString("0011223344556677")

	_, err := Wrap(kek, data)
	if err == nil {
		t.Fatal("Expected failure with too short plaintext", err)
	}

	ciphertext, _ := hex.DecodeString("1FA68B0A8112B447AEF34BD8FB5A7B82")
	_, err = Unwrap(kek, ciphertext)
	if err == nil {
		t.Fatal("Expected failure with too short ciphertext", err)
	}
}
