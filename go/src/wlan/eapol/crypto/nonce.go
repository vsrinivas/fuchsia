// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package crypto

import (
	"math/big"
	"crypto/rand"
	"bytes"
	"encoding/binary"
	"time"
)

// Nonce generation is based on IEEE Std 802.11-2016, 12.7.5.

// TODO(hahnr): Introduce type alias for Nonce.

type NonceReader struct {
	keyCounter *big.Int
	initVal    []byte
}

func NewNonceReader(macAddr [6]byte) *NonceReader {
	reader := &NonceReader{}
	reader.init(macAddr)
	return reader
}

func (k *NonceReader) init(macAddr [6]byte) {
	randNum := make([]byte, 32)
	_, err := rand.Read(randNum)
	if err != nil {
		panic("Error initializing key counter")
	}
	// IEEE Std 802.11-2016, 12.7.5 recommends using a time in NTP format.
	// Fuchsia has no support for NTP yet and instead a regular timestamp is used.
	buf := bytes.Buffer{}
	binary.Write(&buf, binary.BigEndian, time.Now().UnixNano())
	initVal := PRF(randNum, "Init Counter", append(macAddr[:], buf.Bytes()...), 256)
	k.keyCounter = new(big.Int).SetBytes(initVal)
}

// Every call returns a new 32-bit nonce. All nonces are succeeding.
func (k *NonceReader) Read() [32]byte {
	k.keyCounter.Add(k.keyCounter, big.NewInt(1))
	bytes := k.keyCounter.Bytes()

	// If nonce has less than 32 bytes, expand to 32 bytes.
	if len(bytes) < 32 {
		bytes = append(make([]byte, 32 - len(bytes)), bytes...)
	}
	nonce := [32]byte{}
	copy(nonce[:], bytes[len(bytes)-32:])
	return nonce
}