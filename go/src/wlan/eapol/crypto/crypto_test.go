// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package crypto_test

import (
	. "wlan/eapol/crypto"
	"testing"
	"encoding/hex"
	"bytes"
)

// IEEE Std 802.11-2016, J.4.2, Test case 1
func TestPSKTestCase1(t *testing.T) {
	actual, err := PSK("password", "IEEE")
	if err != nil {
		t.Fatalf("Unexpected failure")
	}
	expected, _ := hex.DecodeString("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected PSK generated")
	}
}

// IEEE Std 802.11-2016, J.4.2, Test case 2
func TestPSKTestCase2(t *testing.T) {
	actual, err := PSK("ThisIsAPassword", "ThisIsASSID")
	if err != nil {
		t.Fatalf("Unexpected failure")
	}
	expected, _ := hex.DecodeString("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected PSK generated")
	}
}

// IEEE Std 802.11-2016, J.4.2, Test case 3
func TestPSKTestCase3(t *testing.T) {
	actual, err := PSK("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ")
	if err != nil {
		t.Fatalf("Unexpected failure")
	}
	expected, _ := hex.DecodeString("becb93866bb8c3832cb777c2f559807c8c59afcb6eae734885001300a981cc62")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected PSK generated")
	}
}

func TestPSKTooShortPassPhrase(t *testing.T) {
	_, err := PSK("short", "An SSID")
	if err == nil {
		t.Fatalf("Expected failure with too short passPhrase")
	}
}

func TestPSKTooLongPassPhrase(t *testing.T) {
	_, err := PSK("1234567890123456789012345678901234567890123456789012345678901234", "64 long passPhrase SSID")
	if err == nil {
		t.Fatalf("Expected failure with too long passPhrase")
	}
}

func TestPSKInvalidCharacter(t *testing.T) {
	_, err := PSK("Invalid Char \x1F", "SSID")
	if err == nil {
		t.Fatalf("Expected failure with invalid character in passPhrase")
	}
}

func TestPSKCharacterASCIIBounds(t *testing.T) {
	_, err := PSK("\x20ASCII Bound Test \x7E", "SSID")
	if err != nil {
		t.Fatalf("Failed with allowed characters in passPhrase")
	}
}

func TestPSKUnicodeCharacters(t *testing.T) {
	_, err := PSK("Lorem ipsum \u00DF dolor", "Some SSID")
	if err == nil {
		t.Fatalf("Expected failure with invalid unicode character in passPhrase")
	}
}

func TestPSKUnicodeCharactersCount(t *testing.T) {
	// 5 runes but 10 bytes.
	_, err := PSK("\u00DF\u00DF\u00DF\u00DF\u00DF", "Some SSID")
	if err == nil {
		t.Fatalf("Expected failure with invalid unicode character in passPhrase")
	}
}

// IEEE Std 802.11-2016, J.3.2, Test case 1
func TestPRFTestCase1(t *testing.T) {
	key, _ := hex.DecodeString("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
	actual := PRF(key, "prefix", []byte("Hi There"), 512)
	expected, _ := hex.DecodeString("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.3.2, Test case 2
func TestPRFTestCase2(t *testing.T) {
	key := []byte("Jefe")
	actual := PRF(key, "prefix", []byte("what do ya want for nothing?"), 512)
	expected, _ := hex.DecodeString("51f4de5b33f249adf81aeb713a3c20f4fe631446fabdfa58244759ae58ef9009a99abf4eac2ca5fa87e692c440eb40023e7babb206d61de7b92f41529092b8fc")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.3.2, Test case 3
func TestPRFTestCase3(t *testing.T) {
	key, _ := hex.DecodeString("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
	data := make([]byte, 50)
	for i := 0; i < 50; i++ {
		data[i] = 0xdd
	}
	actual := PRF(key, "prefix", data, 512)
	expected, _ := hex.DecodeString("e1ac546ec4cb636f9976487be5c86be17a0252ca5d8d8df12cfb0473525249ce9dd8d177ead710bc9b590547239107aef7b4abd43d87f0a68f1cbd9e2b6f7607")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.6.5, Test case 1
func TestPRFTestCase65_1(t *testing.T) {
	key, _ := hex.DecodeString("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
	actual := PRF(key, "prefix", []byte("Hi There"), 192)
	expected, _ := hex.DecodeString("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.6.5, Test case 2
func TestPRFTestCase65_2(t *testing.T) {
	key := []byte("Jefe")
	actual := PRF(key, "prefix-2", []byte("what do ya want for nothing?"), 256)
	expected, _ := hex.DecodeString("47c4908e30c947521ad20be9053450ecbea23d3aa604b77326d8b3825ff7475c")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.6.5, Test case 3
func TestPRFTestCase65_3(t *testing.T) {
	key, _ := hex.DecodeString("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
	actual := PRF(key, "prefix-3", []byte("Test Using Larger Than Block-Size Key - Hash Key First"), 384)
	expected, _ := hex.DecodeString("0ab6c33ccf70d0d736f4b04c8a7373255511abc5073713163bd0b8c9eeb7e1956fa066820a73ddee3f6d3bd407e0682a")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.6.5, Test case 4
func TestPRFTestCase65_4(t *testing.T) {
	key, _ := hex.DecodeString("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
	actual := PRF(key, "prefix-4", []byte("Hi There Again"), 512)
	expected, _ := hex.DecodeString("248cfbc532ab38ffa483c8a2e40bf170eb542a2e0916d7bf6d97da2c4c5ca877736c53a65b03fa4b3745ce7613f6ad68e0e4a798b7cf691c96176fd634a59a49")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

func TestPRFEmptyKey(t *testing.T) {
	key := []byte{}
	actual := PRF(key, "something is happening", []byte("Lorem ipsum"), 256)
	expected, _ := hex.DecodeString("5b154287399baeabd7d2c9682989e0933b3fdef8211ae7ae0c6586bb1e38de7c")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

func TestPRFEmptyPrefix(t *testing.T) {
	key, _ := hex.DecodeString("aaaa")
	actual := PRF(key, "", []byte("Lorem ipsum"), 256)
	expected, _ := hex.DecodeString("1317523ae07f212fc4139ce9ebafe31ecf7c59cb07c7a7f04131afe7a59de60c")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

func TestPRFEmptyData(t *testing.T) {
	key, _ := hex.DecodeString("aaaa")
	actual := PRF(key, "some prefix", []byte{}, 192)
	expected, _ := hex.DecodeString("785e095774cfea480c267e74130cb86d1e3fc80095b66554")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

func TestPRFAllEmpty(t *testing.T) {
	key := []byte{}
	actual := PRF(key, "", []byte{}, 128)
	expected, _ := hex.DecodeString("310354661a5962d5b8cb76032d5a97e8")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

func TestPRFNoBits(t *testing.T) {
	key, _ := hex.DecodeString("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
	data := make([]byte, 50)
	for i := 0; i < 50; i++ {
		data[i] = 0xdd
	}
	actual := PRF(key, "prefix", data, 0)
	expected := []byte{}
	if bytes.Compare(actual, expected) != 0 {
		t.Fatalf("Unexpected result")
	}
}

// IEEE Std 802.11-2016, J.7.1, Table J-13 & Table J-15
func TestDeriveKeys(t *testing.T) {
	pmk, _ := hex.DecodeString("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af")
	aNonce, _ := hex.DecodeString("e0e1e2e3e4e5e6e7e8e9f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405")
	sNonce, _ := hex.DecodeString("c0c1c2c3c4c5c6c7c8c9d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5")
	aa, _ := hex.DecodeString("a0a1a1a3a4a5")
	spa, _ := hex.DecodeString("b0b1b2b3b4b5")
	ptk := DeriveKeys(pmk, spa, aa, aNonce, sNonce)

	expectedKCK, _ := hex.DecodeString("379f9852d0199236b94e407ce4c00ec8")
	expectedKEK, _ := hex.DecodeString("47c9edc01c2c6e5b4910caddfb3e51a7")
	expectedTK, _ := hex.DecodeString("b2360c79e9710fdd58bea93deaf06599")
	if bytes.Compare(ptk.KCK, expectedKCK) != 0 {
		t.Fatal("Incorrect KCK: ", hex.EncodeToString(ptk.KCK))
	}
	if bytes.Compare(ptk.KEK, expectedKEK) != 0 {
		t.Fatal("Incorrect KEK: ", hex.EncodeToString(ptk.KEK))
	}
	if bytes.Compare(ptk.TK, expectedTK) != 0 {
		t.Fatal("Incorrect TK: ", hex.EncodeToString(ptk.TK))
	}
}

func TestMinMaxSameLength(t *testing.T) {
	a := []byte{1, 2, 3, 4, 5, 6, 7, 8, 9}
	b := []byte{1, 2, 3, 4, 6, 6, 7, 8, 9}
	if bytes.Compare(Max(a, b), b) != 0 {
		t.Fatal("Expected 'b' to be larger")
	}
	if bytes.Compare(Max(b, a), b) != 0 {
		t.Fatal("Expected 'b' to be larger")
	}
	if bytes.Compare(Min(a, b), a) != 0 {
		t.Fatal("Expected 'a' to be smaller")
	}
	if bytes.Compare(Min(b, a), a) != 0 {
		t.Fatal("Expected 'a' to be smaller")
	}
}

func TestMinMaxDifferentLength(t *testing.T) {
	a := []byte{2, 3, 4, 5, 6, 7, 8, 9}
	b := []byte{1, 2, 3, 4, 6, 6, 7, 8, 9}
	if bytes.Compare(Max(a, b), b) != 0 {
		t.Fatal("Expected 'b' to be larger")
	}
	if bytes.Compare(Max(b, a), b) != 0 {
		t.Fatal("Expected 'b' to be larger")
	}
	if bytes.Compare(Min(a, b), a) != 0 {
		t.Fatal("Expected 'a' to be smaller")
	}
	if bytes.Compare(Min(b, a), a) != 0 {
		t.Fatal("Expected 'a' to be smaller")
	}
}

func TestMinMaxLargeNumbersLength(t *testing.T) {
	a := []byte{0xFE, 0xAF, 0x32, 0x29, 0xCB, 0x12, 0x38, 0xB8,
							0xC3, 0x4A, 0xFE, 0xAF, 0x32, 0x29, 0xCB, 0x12,
							0x38, 0xB8, 0xC3, 0x4A, 0xFE, 0xAF, 0x32, 0x29,
							0x85, 0x12, 0x38, 0x23, 0xC3, 0x4A, 0xFE, 0x22,
							0x32, 0x29, 0xCB, 0xBC, 0x38, 0xB8, 0xC3, 0x4A,
							0xFE, 0xAF, 0xFF, 0x29, 0xCB, 0x12, 0x38, 0xB8,
							0x21, 0x29, 0xCB, 0x12, 0x38, 0xB8, 0xC3, 0x47,
							0xBB, 0xAF, 0x32, 0x29, 0xCB, 0x12, 0x38, 0x30,
	}
	b := []byte{0xFE, 0xAF, 0x32, 0x29, 0xCB, 0x12, 0x38, 0xB9,
							0xC3, 0x4A, 0xFE, 0xAF, 0x32, 0x29, 0xCB, 0x12,
							0x38, 0xB8, 0xC3, 0x4A, 0xFE, 0xAF, 0x32, 0x29,
							0x85, 0x12, 0x38, 0x23, 0xC3, 0x4A, 0xFE, 0x22,
							0x32, 0x29, 0xCB, 0xBC, 0x38, 0xB8, 0xC3, 0x4A,
							0xFE, 0xAF, 0xFF, 0x29, 0xCB, 0x12, 0x38, 0xB8,
							0x21, 0x29, 0xCB, 0x12, 0x38, 0xB8, 0xC3, 0x47,
							0xBB, 0xAF, 0x32, 0x29, 0xCB, 0x12, 0x38, 0x30,
	}
	if bytes.Compare(Max(a, b), b) != 0 {
		t.Fatal("Expected 'b' to be larger")
	}
	if bytes.Compare(Max(b, a), b) != 0 {
		t.Fatal("Expected 'b' to be larger")
	}
	if bytes.Compare(Min(a, b), a) != 0 {
		t.Fatal("Expected 'a' to be smaller")
	}
	if bytes.Compare(Min(b, a), a) != 0 {
		t.Fatal("Expected 'a' to be smaller")
	}
}

func TestMinMaxEmptyNumbers(t *testing.T) {
	a := []byte{}
	b := []byte{}
	if len(Max(a, b)) != 0 {
		t.Fatal("Expected empty result")
	}
	if len(Max(b, a)) != 0 {
		t.Fatal("Expected empty result")
	}
	if len(Min(a, b)) != 0 {
		t.Fatal("Expected empty result")
	}
	if len(Min(b, a)) != 0 {
		t.Fatal("Expected empty result")
	}
}
