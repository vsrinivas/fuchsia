// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package crypto

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha1"
	"fmt"

	"golang.org/x/crypto/pbkdf2"
)

var nonceReader *NonceReader

type PTK struct {
	KCK []byte
	KEK []byte
	TK  []byte
}

// IEEE Std 802.11-2016, J.4.1
func PSK(passPhrase string, ssid string) ([]byte, error) {
	// len(s) can be used because PSK always operates on ASCII characters.
	if len(passPhrase) < 8 || len(passPhrase) > 63 {
		return []byte{}, fmt.Errorf("Expected pass-phrase with 8-63 characters but got %d characters", len(passPhrase))
	}
	for _, c := range passPhrase {
		if c < 32 || c > 126 {
			return []byte{}, fmt.Errorf("Pass-phrase contains invalid character: '%s'(%#x)", string(c), c)
		}
	}
	key := pbkdf2.Key([]byte(passPhrase), []byte(ssid), 4096, 256/8, sha1.New)
	return key, nil
}

// IEEE Std 802.11-2016, 12.7.1.2
// Note: AKM-5, AKM-6, and AKM-11 to AKM-13 use a different PRF definition. Refactor once additional
// AKMs are supported.
func PRF(k []byte, a string, b []byte, bits int) []byte {
	if bits%8 != 0 {
		panic("bits must be a multiple of 8")
	}
	hsha1 := hmac.New(sha1.New, k)
	r := bytes.Buffer{}
	limit := (bits + 159) / 160
	for i := 0; i <= limit; i++ {
		hsha1.Write([]byte(a))
		hsha1.Write([]byte{byte(0)})
		hsha1.Write(b)
		hsha1.Write([]byte{byte(i)})
		r.Write(hsha1.Sum(nil))
		hsha1.Reset()
	}
	return r.Bytes()[:bits/8]
}

// IEEE Std 802.11-2016, 12.7.1.3
// Note: This method is so far AKM-2 specific. Refactor once we support additional AKMs.
func DeriveKeys(pmk, sAddr, aAddr, aNonce, sNonce []byte) PTK {
	var data bytes.Buffer
	data.Write(Min(aAddr, sAddr))
	data.Write(Max(aAddr, sAddr))
	data.Write(Min(aNonce, sNonce))
	data.Write(Max(aNonce, sNonce))
	// TODO(hahnr): TK, KCK, KEK bits must be derived from AKM.
	kckLen := 16
	kekLen := 16
	tkLen := 16
	ptk := PRF(pmk, "Pairwise key expansion", data.Bytes(), (kckLen+kekLen+tkLen)*8)
	return PTK{
		KCK: ptk[:kckLen],
		KEK: ptk[kckLen: kckLen+kekLen],
		TK:  ptk[kckLen+kekLen:],
	}
}

// IEEE Std 802.11-2016, 12.7.1.3
func Max(a, b []byte) []byte {
	if cmpBytes(a, b) > 0 {
		return a
	}
	return b
}

// IEEE Std 802.11-2016, 12.7.1.3
func Min(a, b []byte) []byte {
	if cmpBytes(a, b) < 0 {
		return a
	}
	return b
}

// Returns x > 0 if a > b
// Returns x = 0 if a == b
// Returns x < 0 if a < b
func cmpBytes(a, b []byte) int {
	a = bytes.TrimLeft(a, "\x00")
	b = bytes.TrimLeft(b, "\x00")

	lenDiff := len(a) - len(b)
	if lenDiff == 0 {
		return bytes.Compare(a, b)
	}
	return lenDiff
}

func GetNonce(macAddr [6]byte) [32]byte {
	// Ensure, initialization.
	if nonceReader == nil {
		nonceReader = NewNonceReader(macAddr)
	}
	return nonceReader.Read()
}
