// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elements_test

import (
	. "apps/wlan/wlan/elements"
	"testing"
	"encoding/hex"
	"bytes"
	"reflect"
)

var VendorSpecificOUI = [3]byte{0x01, 0x02, 0x03}

var TEST_CIPHER_1 = CipherSuite{
	Type: CipherSuiteType_BIP_GMAC256,
	OUI:  DefaultCipherSuiteOUI,
}

var TEST_CIPHER_2 = CipherSuite{
	Type: CipherSuiteType_CCMP128,
	OUI:  VendorSpecificOUI,
}

var TEST_CIPHER_3 = CipherSuite{
	Type: CipherSuiteType_WEP40,
	OUI:  DefaultCipherSuiteOUI,
}

var TEST_CIPHER_4 = CipherSuite{
	Type: CipherSuiteType_TKIP,
	OUI:  DefaultCipherSuiteOUI,
}

var TEST_AKM_1 = AKMSuite{
	Type: AkmSuiteType_PSK,
	OUI:  VendorSpecificOUI,
}

var TEST_AKM_2 = AKMSuite{
	Type: AkmSuiteType_FT_8021X_SHA384,
	OUI:  VendorSpecificOUI,
}

var TEST_PMKID_1 = PMKID{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}

func RsnCapability(v int) *uint16 {
	val := uint16(v)
	return &val
}

func TestParseInvalidElement(t *testing.T) {
	// Wrong ElementId.
	raw, _ := hex.DecodeString("40060100000fac02")
	_, err := ParseRSN(raw)
	if err == nil {
		t.Fatal("Expected to refuse element with invalid ID")
	}
}

func TestParseInvalidTooShortElement(t *testing.T) {
	// Wrong ElementId.
	raw, _ := hex.DecodeString("400601")
	_, err := ParseRSN(raw)
	if err == nil {
		t.Fatal("Expected to refuse too short elements")
	}
}

func TestFullRSNE(t *testing.T) {
	rsne := NewEmptyRSN()
	rsne.Version = 1
	rsne.GroupData = &TEST_CIPHER_1
	rsne.PairwiseCiphers = []CipherSuite{TEST_CIPHER_1, TEST_CIPHER_2, TEST_CIPHER_3}
	rsne.AKMs = []AKMSuite{TEST_AKM_1, TEST_AKM_2}
	rsne.Caps = RsnCapability(1337)
	rsne.PMKIDs = []PMKID{TEST_PMKID_1}
	rsne.GroupMgmt = &TEST_CIPHER_4
	actual := rsne.Bytes()
	expected, _ := hex.DecodeString("30360100000fac0c0300000fac0c01020304000fac010200010203020102030d39050100000102030405060708090a0b0c0d0e0f000fac02")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatal("RSNE byte representation differs from expected one")
	}

	parsedRSNE, err := ParseRSN(expected)
	if err != nil {
		t.Fatal("Parsing RSNE failed")
	}
	AssertRSNEsEquality(rsne, parsedRSNE, t)
}

func TestNoCapabilitiesRSNE(t *testing.T) {
	rsne := NewEmptyRSN()
	rsne.Version = 1
	rsne.GroupData = &TEST_CIPHER_1
	rsne.PairwiseCiphers = []CipherSuite{TEST_CIPHER_1, TEST_CIPHER_2}
	rsne.AKMs = []AKMSuite{TEST_AKM_1, TEST_AKM_2}
	actual := rsne.Bytes()
	expected, _ := hex.DecodeString("301a0100000fac0c0200000fac0c010203040200010203020102030d")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatal("Converting to bytes failed")
	}

	parsedRSNE, err := ParseRSN(expected)
	if err != nil {
		t.Fatal("Parsing RSNE failed")
	}
	AssertRSNEsEquality(rsne, parsedRSNE, t)
}

// Announces two pairwise ciphers but only specifies one
func TestParseWithInvalidPairwiseList(t *testing.T) {
	rsne := NewEmptyRSN()
	rsne.Version = 1
	rsne.GroupData = &TEST_CIPHER_1
	raw, _ := hex.DecodeString("301a0100000fac0c0200000fac0c")
	parsedRSNE, err := ParseRSN(raw)
	if err != nil {
		t.Fatal("Parsing RSNE failed")
	}
	AssertRSNEsEquality(rsne, parsedRSNE, t)
}

func TestEmptyCapabilitiesRSNE(t *testing.T) {
	rsne := NewEmptyRSN()
	rsne.Version = 1
	rsne.GroupData = &TEST_CIPHER_2
	rsne.PairwiseCiphers = []CipherSuite{TEST_CIPHER_3}
	rsne.AKMs = []AKMSuite{TEST_AKM_1}
	rsne.Caps = RsnCapability(0)
	actual := rsne.Bytes()
	expected, _ := hex.DecodeString("30140100010203040100000fac010100010203020000")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatal("Converting to bytes failed")
	}

	parsedRSNE, err := ParseRSN(expected)
	if err != nil {
		t.Fatal("Parsing RSNE failed")
	}
	AssertRSNEsEquality(rsne, parsedRSNE, t)
}

func TestNoOptionalFieldsRSNE(t *testing.T) {
	rsne := NewEmptyRSN()
	rsne.Version = 1
	actual := rsne.Bytes()
	expected, _ := hex.DecodeString("30020100")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatal("Converting to bytes failed")
	}

	parsedRSNE, err := ParseRSN(expected)
	if err != nil {
		t.Fatal("Parsing RSNE failed")
	}
	AssertRSNEsEquality(rsne, parsedRSNE, t)
}

// AKMs and Pairwise Ciphers are expected to be truncated.
func TestNoPairwiseButAKMsRSNE(t *testing.T) {
	rsne := NewEmptyRSN()
	rsne.Version = 1
	rsne.GroupData = &TEST_CIPHER_4
	rsne.PairwiseCiphers = []CipherSuite{}
	rsne.AKMs = []AKMSuite{TEST_AKM_1}
	actual := rsne.Bytes()
	expected, _ := hex.DecodeString("30060100000fac02")
	if bytes.Compare(actual, expected) != 0 {
		t.Fatal("Converting to bytes failed")
	}

	// Expected RSN should contain neither an AKM nor Pairwise cipher.
	rsne = NewEmptyRSN()
	rsne.Version = 1
	rsne.GroupData = &TEST_CIPHER_4
	parsedRSNE, err := ParseRSN(expected)
	if err != nil {
		t.Fatal("Parsing RSNE failed")
	}
	AssertRSNEsEquality(rsne, parsedRSNE, t)
}

func AssertRSNEsEquality(rsne1 *RSN, rsne2 *RSN, t *testing.T) {
	if rsne1.Version != rsne2.Version {
		t.Fatal("Different Version")
	}
	if !reflect.DeepEqual(rsne1.GroupData, rsne2.GroupData) {
		t.Fatal("Different GroupData")
	}
	if !reflect.DeepEqual(rsne1.PairwiseCiphers, rsne2.PairwiseCiphers) {
		t.Fatal("Different PairwiseCiphers")
	}
	if !reflect.DeepEqual(rsne1.AKMs, rsne2.AKMs) {
		t.Fatal("Different AKMs")
	}
	if !reflect.DeepEqual(rsne1.Caps, rsne2.Caps) {
		t.Fatal("Different Capabilities")
	}
	if !reflect.DeepEqual(rsne1.GroupMgmt, rsne2.GroupMgmt) {
		t.Fatal("Different GroupMgmt")
	}
}