// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: Marius Schilder
//
// Lightweight C crypto library for sha256, hmac-sha256, DH,
// RSA2K-SHA256-PKCS15, PRNG, sha1, hmac-sha1, RSA2K-SHA1-PKCS15.
//
// Plain C, no system calls, no malloc.

#pragma once

#include <inttypes.h>
#include <zircon/compiler.h>

#define clBIGNUMBYTES 256      // 2048 bit key length max
#define clBIGNUMWORDS (clBIGNUMBYTES / sizeof(uint32_t))

__BEGIN_CDECLS

struct clHASH_CTX;  // forward decl.

// RSA interface ----------------------------------------

typedef struct clBignumModulus {
  int size;                    // Length of n[] in bytes;
  int nwords;                  // Length of n[] in number of uint32_t
  uint32_t n0inv;              // -1 / n[0] mod 2^32
  uint32_t n[clBIGNUMWORDS];   // modulus as little endian array
  uint32_t rr[clBIGNUMWORDS];  // 2^(2*32*nwords) mod n as little endian array
} clBignumModulus;

// PKCS1.5 signature verify.
// signature_len must be key->size.
// Returns 1 if OK. Trashes hash.
int clRSA2K_verify(const clBignumModulus* key,
                   const uint8_t* signature,
                   const int signature_len,
                   struct clHASH_CTX* hash /* not const! */);

// Generic hash interface ----------------------------------------

typedef struct clHASH_vtab {
  void (* const init)(struct clHASH_CTX*);
  void (* const update)(struct clHASH_CTX*, const void*, int);
  const uint8_t* (* const final)(struct clHASH_CTX*);
  void (* const _transform)(struct clHASH_CTX*);
  const int size;
  const uint8_t* _2Kpkcs15hashpad;  // hash of 2K bit padding.
} clHASH_vtab;

typedef struct clHASH_CTX {
  const clHASH_vtab* f;
  uint64_t count;
  uint8_t buf[64];
  uint32_t state[8];
} clHASH_CTX;

#define clHASH_init(ctx) (ctx)->f->init(ctx)
#define clHASH_update(ctx, data, len) (ctx)->f->update(ctx, data, len)
#define clHASH_final(ctx) (ctx)->f->final(ctx)
#define clHASH_size(ctx) (ctx)->f->size
#define clHASH_MAX_DIGEST_SIZE 32

// Generic hmac interface ----------------------------------------

typedef struct clHMAC_CTX {
  clHASH_CTX hash;
  uint8_t opad[64];
} clHMAC_CTX;

#define clHMAC_update(ctx, data, len) clHASH_update(&(ctx)->hash, data, len)
#define clHMAC_size(ctx) clHASH_size(&(ctx)->hash)
const uint8_t* clHMAC_final(clHMAC_CTX* ctx);

// SHA1 interface ----------------------------------------------

#define clSHA1_DIGEST_SIZE 20
typedef clHASH_CTX clSHA1_CTX;

void clSHA1_init(clSHA1_CTX* ctx);
void clHMAC_SHA1_init(clHMAC_CTX* ctx, const void* key, int len);
const uint8_t* clSHA1(const void* data, int len, uint8_t* digest);

// SHA256 interface --------------------------------------------

#define clSHA256_DIGEST_SIZE 32
typedef clHASH_CTX clSHA256_CTX;

void clSHA256_init(clSHA256_CTX* ctx);
void clHMAC_SHA256_init(clHMAC_CTX* ctx, const void* key, int len);
const uint8_t* clSHA256(const void* data, int len, uint8_t* digest);

// Safe compare interface --------------------------------

// Returns 0 if equal.
// Only fixed timing if arrays are of same length!
int clEqual(const uint8_t* a, int a_len, const uint8_t* b, int b_len);

// DH interface --------------------------------------------

// Computes 2 ** x into out. x and out bigendian byte strings.
// out must be able to hold mod->size bytes.
// Return 0 on error. (invalid value for x).
int clDHgenerate(const clBignumModulus* mod,
                 const uint8_t* x, const int size_x,
                 uint8_t* out);

// Computes gy ** x into out. gy, x, and out bigendian byte strings.
// size_gy must be mod->size.
// Returns 0 on error. (invalid size_gy, gy, x).
int clDHcompute(const clBignumModulus* mod,
                const uint8_t* gy, const int size_gy,
                const uint8_t* x, const int size_x,
                uint8_t* out);

// PRNG interface --------------------------------------------

typedef struct clPRNG_CTX {
  uint8_t v[clSHA256_DIGEST_SIZE * 2];
  int index;
} clPRNG_CTX;

// Initial seeding.
void clPRNG_init(clPRNG_CTX* ctx, const void* data, int size);

// Add entropy to state. Non-destructive, additive.
// Best to call at least once before calling clPRNG_draw().
void clPRNG_entropy(clPRNG_CTX* ctx, const void* data, int size);

// Generate size bytes random and advance state.
// Beware: out value covers entire spectrum so all 0 is possible.
void clPRNG_draw(clPRNG_CTX* ctx, void* out, int size);

__END_CDECLS
