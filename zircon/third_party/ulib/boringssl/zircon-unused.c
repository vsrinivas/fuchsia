// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <openssl/aes.h>
#include <openssl/base.h>

#include "crypto/fipsmodule/modes/internal.h"

// The functions in this file are symbols that appear in Zircon's uboringssl but never used.
// In some linking approaches the build can't determine that they are unused and will fail with an
// 'unresolved symbol' error.  The simplest solution that keeps the footprint of uboringssl as small
// as possible is to simply abort if any of these unused functions are in fact used.

void CRYPTO_cbc128_decrypt(const uint8_t* in, uint8_t* out, size_t len, const AES_KEY* key,
                           uint8_t ivec[16], block128_f block) {
    abort();
}

void CRYPTO_cbc128_encrypt(const uint8_t* in, uint8_t* out, size_t len, const AES_KEY* key,
                           uint8_t ivec[16], block128_f block) {
    abort();
}

void CRYPTO_cfb128_encrypt(const uint8_t* in, uint8_t* out, size_t len, const AES_KEY* key,
                           uint8_t ivec[16], unsigned* num, int enc, block128_f block) {
    abort();
}

void CRYPTO_ctr128_encrypt(const uint8_t* in, uint8_t* out, size_t len, const AES_KEY* key,
                           uint8_t ivec[16], uint8_t ecount_buf[16], unsigned* num,
                           block128_f block) {
    abort();
}

void CRYPTO_ctr128_encrypt_ctr32(const uint8_t* in, uint8_t* out, size_t len, const AES_KEY* key,
                                 uint8_t ivec[16], uint8_t ecount_buf[16], unsigned* num,
                                 ctr128_f ctr) {
    abort();
}

void CRYPTO_ofb128_encrypt(const uint8_t* in, uint8_t* out, size_t len, const AES_KEY* key,
                           uint8_t ivec[16], unsigned* num, block128_f block) {
    abort();
}

int MD4_Final(unsigned char* md, MD4_CTX* c) {
    abort();
}

int MD4_Init(MD4_CTX* c) {
    abort();
}

int MD4_Update(MD4_CTX* c, const void* data, size_t len) {
    abort();
}

int MD5_Final(unsigned char* md, MD5_CTX* c) {
    abort();
}

int MD5_Init(MD5_CTX* c) {
    abort();
}

int MD5_Update(MD5_CTX* c, const void* data, size_t len) {
    abort();
}

int RAND_bytes(uint8_t* out, size_t out_len) {
    abort();
}

int SHA1_Final(uint8_t* md, SHA_CTX* sha) {
    abort();
}

int SHA1_Init(SHA_CTX* sha) {
    abort();
}

int SHA1_Update(SHA_CTX* sha, const void* data, size_t len) {
    abort();
}

int SHA384_Final(uint8_t* md, SHA512_CTX* sha) {
    abort();
}

int SHA384_Init(SHA512_CTX* sha) {
    abort();
}

int SHA384_Update(SHA512_CTX* sha, const void* data, size_t len) {
    abort();
}

int SHA512_Final(uint8_t* md, SHA512_CTX* sha) {
    abort();
}

int SHA512_Init(SHA512_CTX* sha) {
    abort();
}

int SHA512_Update(SHA512_CTX* sha, const void* data, size_t len) {
    abort();
}
