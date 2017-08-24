// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package keywrap

import (
	"bytes"
	"crypto/aes"
	"encoding/binary"
	"errors"
)

var (
	ErrInvalidTextLength = errors.New("text length must be of multiple 64-bit blocks and at least 128 bits")
	ErrInvalidKeySize    = errors.New("invalid key size")
	ErrCorruptedKeyData  = errors.New("key data corrupted")
)

// Implementation of RFC 3394 - Advanced Encryption Standard (AES) Key Wrap Algorithm

// RFC 3394, 2.2.3
var DefaultIv = []byte{0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6}

// RFC 3394, 2.2.1 - Uses index based wrapping
func Wrap(key, plaintext []byte) (r []byte, err error) {
	n := len(plaintext) / 8
	if len(plaintext)%8 != 0 || n < 2 {
		err = ErrInvalidTextLength
		return
	}
	cipher, err := aes.NewCipher(key)
	if err != nil {
		err = ErrInvalidKeySize
		return
	}
	b := make([]byte, cipher.BlockSize())

	// 1) Initialize variables
	// a[:8] = A, while a[:] = A | R[i] used as temporary variable to encrypt the ciphertext.
	// Hence, make a large enough buffer to hold an entire block.
	a := make([]byte, cipher.BlockSize())
	copy(a, DefaultIv)
	r = make([]byte, (n+1)*8)
	copy(r[8:], plaintext)

	// 2) Calculate intermediate values
	for j := 0; j <= 5; j++ {
		for i := 1; i <= n; i++ {
			// a[:] = A | R[i]
			ri := r[i*8 : (i*8)+8]
			copy(a[8:], ri)
			cipher.Encrypt(b, a)
			t := uint64(n*j + i)
			binary.BigEndian.PutUint64(a, binary.BigEndian.Uint64(b[:8])^t)
			copy(ri, b[8:])
		}
	}

	// 3) Output the results
	copy(r, a[:8])

	return
}

// RFC 3394, 2.2.2 - uses index based unwrapping
func Unwrap(key, ciphertext []byte) (r []byte, err error) {
	n := len(ciphertext)/8 - 1
	if len(ciphertext)%8 != 0 || n < 2{
		err = ErrInvalidTextLength
		return
	}
	cipher, err := aes.NewCipher(key)
	if err != nil {
		err = ErrInvalidKeySize
		return
	}
	b := make([]byte, cipher.BlockSize())

	// 1) Initialize variables
	// a[:8] = A, while a[:] = (A ^ t) | R[i]used as temporary variable to decrypt the ciphertext.
	// Hence, make a large enough buffer to hold an entire block.
	a := make([]byte, cipher.BlockSize())
	copy(a, ciphertext)
	r = make([]byte, n*8)
	copy(r, ciphertext[8:])

	// 2) Compute intermediate values
	for j := 5; j >= 0; j-- {
		for i := n; i >= 1; i-- {
			t := uint64(n*j + i)
			binary.BigEndian.PutUint64(a, binary.BigEndian.Uint64(a)^t)
			ri := r[(i-1)*8 : i*8]
			copy(a[8:], ri)
			cipher.Decrypt(b, a)
			copy(a[:8], b[:8])
			copy(ri, b[8:])
		}
	}

	// 3) Output results
	if bytes.Compare(a[:8], DefaultIv) != 0 {
		r = nil
		err = ErrCorruptedKeyData
	}
	return
}
