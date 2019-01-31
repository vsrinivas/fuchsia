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

#include <lib/crypto/cryptolib.h>

#include <string.h>

// Generic HASH code section ===========================================

static void _HASH_update(clHASH_CTX* ctx, const void* data, int len) {
  int i = (int) (ctx->count & 63);
  const uint8_t* p = (const uint8_t*)data;

  ctx->count += len;

  while (len--) {
    ctx->buf[i++] = *p++;
    if (i == 64) {
      ctx->f->_transform(ctx);
      i = 0;
    }
  }
}

static const uint8_t* _HASH_final(clHASH_CTX* ctx) {
  uint8_t *p = ctx->buf;
  uint64_t cnt = ctx->count * 8;
  int i;

  clHASH_update(ctx, (const uint8_t*)"\x80", 1);
  while ((ctx->count & 63) != 56) {
    clHASH_update(ctx, (const uint8_t*)"\0", 1);
  }
  for (i = 0; i < 8; ++i) {
    uint8_t tmp = (uint8_t) (cnt >> ((7 - i) * 8));
    clHASH_update(ctx, &tmp, 1);
  }

  for (i = 0; i < clHASH_size(ctx) / 4; i++) {
    uint32_t tmp = ctx->state[i];
    *p++ = tmp >> 24;
    *p++ = tmp >> 16;
    *p++ = tmp >> 8;
    *p++ = tmp >> 0;
  }

  return ctx->buf;
}

// Generic HMAC code section ===========================================

static void _HMAC_init(clHMAC_CTX* ctx, const void* key, int len) {
  unsigned int i;
  memset(&ctx->opad[0], 0, sizeof(ctx->opad));

  if ((unsigned int) len > sizeof(ctx->opad)) {
    clHASH_init(&ctx->hash);
    clHASH_update(&ctx->hash, key, len);
    memcpy(&ctx->opad[0], clHASH_final(&ctx->hash), clHASH_size(&ctx->hash));
  } else {
    memcpy(&ctx->opad[0], key, len);
  }

  for (i = 0; i < sizeof(ctx->opad); ++i) {
    ctx->opad[i] ^= 0x36;
  }

  clHASH_init(&ctx->hash);
  clHASH_update(&ctx->hash, ctx->opad, sizeof(ctx->opad));  // hash ipad

  for (i = 0; i < sizeof(ctx->opad); ++i) {
    ctx->opad[i] ^= (0x36 ^ 0x5c);
  }
}

const uint8_t* clHMAC_final(clHMAC_CTX* ctx) {
  uint8_t digest[clHASH_MAX_DIGEST_SIZE];
  memcpy(digest, clHASH_final(&ctx->hash), sizeof(digest));
  clHASH_init(&ctx->hash);
  clHASH_update(&ctx->hash, ctx->opad, sizeof(ctx->opad));
  clHASH_update(&ctx->hash, digest, sizeof(digest));
  memset(&ctx->opad[0], 0, sizeof(ctx->opad));  // wipe key
  return clHASH_final(&ctx->hash);
}

// Fixed timing comparision function ====================================

int clEqual(const uint8_t* a, int a_len, const uint8_t* b, int b_len) {
  int i, c = a_len - b_len;
  for (i = 0; i < a_len && i < b_len; ++i) {
    c |= a[i] - b[i];
  }
  return c;
}

// SHA256 code section ==================================================

static void _SHA256_transform(clHASH_CTX* ctx) {
  static const uint32_t _SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

#define _ROR(value, bits) (((value) >> (bits)) | ((value) << (32 - (bits))))
#define _SHR(value, bits) ((value) >> (bits))

  uint32_t W[64];
  uint32_t A, B, C, D, E, F, G, H;
  const uint8_t* p = ctx->buf;
  int t;

  for(t = 0; t < 16; ++t) {
    uint32_t tmp =  *p++ << 24;
    tmp |= *p++ << 16;
    tmp |= *p++ << 8;
    tmp |= *p++;
    W[t] = tmp;
  }

  for(; t < 64; t++) {
    uint32_t s0 = _ROR(W[t-15], 7) ^ _ROR(W[t-15], 18) ^ _SHR(W[t-15], 3);
    uint32_t s1 = _ROR(W[t-2], 17) ^ _ROR(W[t-2], 19) ^ _SHR(W[t-2], 10);
    W[t] = W[t-16] + s0 + W[t-7] + s1;
  }

  A = ctx->state[0];
  B = ctx->state[1];
  C = ctx->state[2];
  D = ctx->state[3];
  E = ctx->state[4];
  F = ctx->state[5];
  G = ctx->state[6];
  H = ctx->state[7];

  for(t = 0; t < 64; t++) {
    uint32_t s0 = _ROR(A, 2) ^ _ROR(A, 13) ^ _ROR(A, 22);
    uint32_t maj = (A & B) ^ (A & C) ^ (B & C);
    uint32_t t2 = s0 + maj;
    uint32_t s1 = _ROR(E, 6) ^ _ROR(E, 11) ^ _ROR(E, 25);
    uint32_t ch = (E & F) ^ ((~E) & G);
    uint32_t t1 = H + s1 + ch + _SHA256_K[t] + W[t];

    H = G;
    G = F;
    F = E;
    E = D + t1;
    D = C;
    C = B;
    B = A;
    A = t1 + t2;
  }

  ctx->state[0] += A;
  ctx->state[1] += B;
  ctx->state[2] += C;
  ctx->state[3] += D;
  ctx->state[4] += E;
  ctx->state[5] += F;
  ctx->state[6] += G;
  ctx->state[7] += H;

#undef _SHR
#undef _ROR
}

const uint8_t* clSHA256(const void* data, int len, uint8_t* digest) {
  clSHA256_CTX ctx;
  clSHA256_init(&ctx);
  clHASH_update(&ctx, data, len);
  memcpy(digest, clHASH_final(&ctx), clSHA256_DIGEST_SIZE);
  return digest;
}

// SHA256 of PKCS1.5 signature padding for 2048 bit RSA,
// as per openssl, RSA_PKCS1_PADDING, EVP_sha256() hash.
// At the location of the bytes of the hash all 00 are hashed.
static const uint8_t kExpectedPadRsa2kSha256[clSHA256_DIGEST_SIZE] = {
  0xab, 0x28, 0x8d, 0x8a, 0xd7, 0xd9, 0x59, 0x92,
  0xba, 0xcc, 0xf8, 0x67, 0x20, 0xe1, 0x15, 0x2e,
  0x39, 0x8d, 0x80, 0x36, 0xd6, 0x6f, 0xf0, 0xfd,
  0x90, 0xe8, 0x7d, 0x8b, 0xe1, 0x7c, 0x87, 0x59
};

static const clHASH_vtab _SHA256_vtab = {
  clSHA256_init,
  _HASH_update,
  _HASH_final,
  _SHA256_transform,
  clSHA256_DIGEST_SIZE,
  kExpectedPadRsa2kSha256
};

void clSHA256_init(clSHA256_CTX* ctx) {
  ctx->f = &_SHA256_vtab;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
  ctx->count = 0;
}

void clHMAC_SHA256_init(clHMAC_CTX* ctx, const void* key, int len) {
  clSHA256_init(&ctx->hash);
  _HMAC_init(ctx, key, len);
}

// SHA1 code section =====================================================

static void _SHA1_transform(clHASH_CTX* ctx) {
#define _ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
  uint32_t W[80];
  uint32_t A, B, C, D, E;
  const uint8_t* p = ctx->buf;
  int t;

  for(t = 0; t < 16; ++t) {
    uint32_t tmp =  *p++ << 24;
    tmp |= *p++ << 16;
    tmp |= *p++ << 8;
    tmp |= *p++;
    W[t] = tmp;
  }

  for(; t < 80; t++) {
    W[t] = _ROL(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16],1);
  }

  A = ctx->state[0];
  B = ctx->state[1];
  C = ctx->state[2];
  D = ctx->state[3];
  E = ctx->state[4];

  for(t = 0; t < 80; t++) {
    uint32_t tmp = _ROL(A,5) + E + W[t];

    if (t < 20)
      tmp += (D^(B&(C^D))) + 0x5A827999;
    else if ( t < 40)
      tmp += (B^C^D) + 0x6ED9EBA1;
    else if ( t < 60)
      tmp += ((B&C)|(D&(B|C))) + 0x8F1BBCDC;
    else
      tmp += (B^C^D) + 0xCA62C1D6;

    E = D;
    D = C;
    C = _ROL(B,30);
    B = A;
    A = tmp;
  }

  ctx->state[0] += A;
  ctx->state[1] += B;
  ctx->state[2] += C;
  ctx->state[3] += D;
  ctx->state[4] += E;
#undef _ROL
}

// SHA1 of PKCS1.5 signature padding for 2048 bit RSA,
// as per openssl, RSA_PKCS1_PADDING, EVP_sha1() hash.
// At the location of the bytes of the hash all 00 are hashed.
static const uint8_t kExpectedPadRsa2kSha1[clSHA1_DIGEST_SIZE] = {
  0xdc, 0xbd, 0xbe, 0x42, 0xd5, 0xf5, 0xa7, 0x2e, 0x6e, 0xfc,
  0xf5, 0x5d, 0xaf, 0x9d, 0xea, 0x68, 0x7c, 0xfb, 0xf1, 0x67
};

static const clHASH_vtab _SHA1_vtab = {
  clSHA1_init,
  _HASH_update,
  _HASH_final,
  _SHA1_transform,
  clSHA1_DIGEST_SIZE,
  kExpectedPadRsa2kSha1
};

void clSHA1_init(clSHA1_CTX* ctx) {
  ctx->f = &_SHA1_vtab;
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;
  ctx->count = 0;
}

void clHMAC_SHA1_init(clHMAC_CTX* ctx, const void* key, int len) {
  clSHA1_init(&ctx->hash);
  _HMAC_init(ctx, key, len);
}

const uint8_t* clSHA1(const void* data, int len, uint8_t* digest) {
  clSHA1_CTX ctx;
  clSHA1_init(&ctx);
  clHASH_update(&ctx, data, len);
  memcpy(digest, clHASH_final(&ctx), clSHA1_DIGEST_SIZE);
  return digest;
}

// Bignum code section ====================================================

// c[] = a[] - mod, fixed timing
// mask either 0 or 1.
// Returns unmodified input a[] if borrow, modified output c[] if no borrow.
static const uint32_t* subM(const clBignumModulus * mod,
                            uint32_t* c,
                            const uint32_t* a,
                            uint32_t mask) {
  int64_t A = 0;
  int64_t offset = a - c;
  int i;
  for (i = 0; i < mod->nwords; ++i) {
    A += (uint64_t)a[i] - (mod->n[i] * mask);
    c[i] = (uint32_t)A;
    A >>= 32;
  }
  return c + (offset & A);
}

// montgomery c[] += a * b[] / R % mod, fixed timing.
static void montMulAdd(const clBignumModulus * mod,
                       uint32_t* c,
                       const uint32_t a,
                       const uint32_t* b) {
  uint64_t A = (uint64_t)a * b[0] + c[0];
  uint32_t d0 = (uint32_t)A * mod->n0inv;
  uint64_t B = (uint64_t)d0 * mod->n[0] + (uint32_t)A;
  int i;

  for (i = 1; i < mod->nwords; ++i) {
    A = (A >> 32) + (uint64_t)a * b[i] + c[i];
    B = (B >> 32) + (uint64_t)d0 * mod->n[i] + (uint32_t)A;
    c[i - 1] = (uint32_t)B;
  }

  A = (A >> 32) + (B >> 32);

  c[i - 1] = (uint32_t)A;

  subM(mod, c, c, (uint32_t)(A >> 32));  // A >> 32 either 0 or 1.
}

// montgomery c[] = a[] * b[] / R % mod, fixed timing.
static void montMul(const clBignumModulus * mod,
                    uint32_t* c,
                    const uint32_t* a,
                    const uint32_t* b) {
  int i;
  memset(c, 0, mod->size);
  for (i = 0; i < mod->nwords; ++i) {
    montMulAdd(mod, c, a[i], b);
  }
}

// Convert from lsw first uint32_t to msb first uint8_t.
// len in words.
static void u32tou8(uint8_t* dst, const uint32_t* src, int len) {
  int i;
  dst += len * 4;
  for (i = 0; i < len; ++i) {
    *--dst = (src[i] & 0x000000ff) >> 0;
    *--dst = (src[i] & 0x0000ff00) >> 8;
    *--dst = (src[i] & 0x00ff0000) >> 16;
    *--dst = (src[i] & 0xff000000) >> 24;
  }
}

// Convert from msb first uint8_t to lsw first uint32_t.
// src_len in bytes, multiple of 4.
static void u8tou32(uint32_t* dst, const uint8_t* src, int src_len) {
  int i;
  src += src_len;
  for (i = 0; i < src_len; i += 4) {
    *dst  = (*--src & 0xff) << 0;
    *dst |= (*--src & 0xff) << 8;
    *dst |= (*--src & 0xff) << 16;
    *dst |= (*--src & 0xff) << 24;
    dst++;
  }
}

// In-place exponentiation to power 65537.
// Input and output big-endian byte array in inout. Fixed timing.
static void modpowF4(const clBignumModulus* key, uint8_t* inout) {
  uint32_t a[clBIGNUMWORDS];
  uint32_t aR[clBIGNUMWORDS];
  uint32_t aaR[clBIGNUMWORDS];
  uint32_t* aaa = aaR;  // Re-use location.
  int i;

  u8tou32(a, inout, key->size);

  montMul(key, aR, a, key->rr);  // aR = a * RR / R mod M
  for (i = 0; i < 16; i += 2) {
    montMul(key, aaR, aR, aR);  // aaR = aR * aR / R mod M
    montMul(key, aR, aaR, aaR);  // aR = aaR * aaR / R mod M
  }
  montMul(key, aaa, aR, a);  // aaa = aR * a / R mod M

  u32tou8(inout, subM(key, a, aaa, 1), key->nwords);
}

// Verify a 2048 bit RSA PKCS1.5 signature against an expected hash.
// Returns 0 on failure, 1 on success. NOT-fixed timing!
int clRSA2K_verify(const clBignumModulus* key,
                   const uint8_t* signature,
                   const int len,
                   clHASH_CTX* hash) {
  uint8_t buf[clBIGNUMBYTES];
  const uint8_t* digest = clHASH_final(hash);
  int i;

  if (key->nwords != clBIGNUMWORDS) return 0;  // Wrong key passed in.
  if (len != sizeof(buf)) return 0;  // Wrong input length.

  memcpy(buf, signature, sizeof buf);

  modpowF4(key, buf);  // In-place exponentiation to power 65537.

  // Xor digest location, so all bytes becomes 0 if equal.
  for (i = len - clHASH_size(hash); i < len; ++i) {
    buf[i] ^= *digest++;
  }

  // Hash resulting buffer.
  clHASH_init(hash);
  clHASH_update(hash, buf, len);
  digest = clHASH_final(hash);

  // This should equal hash of pkcs15 padding.
  return clEqual(digest, clHASH_size(hash),
                 hash->f->_2Kpkcs15hashpad, clHASH_size(hash)) == 0;
}

// DH code section ========================================

// c[] = a[] * 1 / R mod M, fixed timing.
static void montMul1(const clBignumModulus* M,
                     uint32_t* c,
                     const uint32_t* a) {
  int i;
  memset(c, 0, M->size);
  montMulAdd(M, c, 1, a);
  for (i = 1; i < M->nwords; ++i)
    montMulAdd(M, c, 0, a);
}

// c = a[] ** x mod M, fixed timing.
// c, x bigendian
static void modExp(const clBignumModulus* M,
                   uint8_t* c,
                   const uint32_t* a,
                   const uint8_t* x, const int size_x) {
  uint32_t tmp[clBIGNUMWORDS];
  uint32_t base[clBIGNUMWORDS];
  uint32_t one[clBIGNUMWORDS];  // Could be const member of M to save stack.
  uint32_t accu[clBIGNUMWORDS];
  int64_t offset = base - one;
  int i, b;

  montMul1(M, one, M->rr);  // 1 * RR / R mod M == R mod M aka '1'
  montMul(M, base, a, M->rr);  // base = a * R mod M
  montMul1(M, accu, M->rr);  // accu = 1 * RR / R = R mod M aka '1'
  montMul1(M, tmp, M->rr);  // tmp = 1 * RR / R = R mod M aka '1'

  for (i = 0; i < size_x; ++i) {
    for (b = 7; b >= 0; --b) {
      // Always multiply, either with base or one.
      // This should keep timing reasonably constant at cost of efficiency.
      // Does _not_ protect against L1 cache sharing timing channels.
      int64_t mask = 0 - ((x[i] >> b) & 1);
      montMul(M, tmp, accu, one + (offset & mask));
      montMul(M, accu, tmp, tmp);
    }
  }

  montMul1(M, accu, tmp);  // accu = 1 * tmp * R / R mod M; undo last sqr.
  u32tou8(c, subM(M, tmp, accu, 1), M->nwords);
}

static const uint32_t dh_G[clBIGNUMWORDS] = { 2 };  // Hardcoded DH generator.

// Returns a[] >= b[]
static int dhGE(const uint32_t* a, const uint32_t* b, int nwords) {
  int64_t A = 0;
  int i;
  for (i = 0; i < nwords; ++i) {
    A += (uint64_t)a[i] - b[i];
    A >>= 32;
  }
  return A == 0;  // 0 == no borrow, hence >=.
}

// Returns 2 <= n < M->n - 1
static int dhCheck(const clBignumModulus* M, const uint32_t* n) {
  uint32_t Mmin1[clBIGNUMWORDS];
  if (!dhGE(n, dh_G, M->nwords)) return 0;  // n >= 2?
  memcpy(Mmin1, M->n, sizeof Mmin1);
  Mmin1[0] -= 1;  // M->n odd, so just decrementing Mmin1[0] works.
  return !dhGE(n, Mmin1, M->nwords);  // n < n - 1?
}

int clDHgenerate(const clBignumModulus* M,
                 const uint8_t* x, const int size_x,
                 uint8_t* out) {
  uint32_t chk[clBIGNUMWORDS];
  modExp(M, out, dh_G, x, size_x);
  // Make sure we didn't compute a value outside [2..M-1>
  u8tou32(chk, out, M->size);
  if (!dhCheck(M, chk)) return 0;
  return 1;
}

int clDHcompute(const clBignumModulus* M,
                const uint8_t* gy, const int size_gy,
                const uint8_t* x, const int size_x,
                uint8_t* out) {
  uint32_t base[clBIGNUMWORDS];
  if (size_gy != M->size) return 0;
  u8tou32(base, gy, size_gy);
  // Make sure the other party's value is inside [2..M-1>
  if (!dhCheck(M, base)) return 0;
  modExp(M, out, base, x, size_x);
  return 1;
}

// PRNG code section ==================================================

void clPRNG_init(clPRNG_CTX* ctx, const void* data, int size) {
  ctx->index = 0;
  memset(ctx->v, 0, sizeof(ctx->v));
  clPRNG_entropy(ctx, data, size);
}

void clPRNG_entropy(clPRNG_CTX* ctx, const void* data, int size) {
  const uint8_t* p = (const uint8_t*)data;
  while (size-- > 0) {
    ctx->v[ctx->index++ % sizeof (ctx->v)] ^= *p++;
  }
}

void clPRNG_draw(clPRNG_CTX* ctx, void* out, int size) {
  const uint8_t* digest;
  uint8_t* output = (uint8_t*)out;
  const uint8_t* rnd;
  clHMAC_CTX hmac;
  int i;

  while (size > 0) {
    // compute output: out = hmac(v, v0);
    clHMAC_SHA256_init(&hmac, ctx->v, sizeof(ctx->v));
    clHMAC_update(&hmac, ctx->v, clSHA256_DIGEST_SIZE);
    rnd = clHMAC_final(&hmac);

    for (i = 0; (i < clSHA256_DIGEST_SIZE) && (size > 0); ++i) {
      *output++ = *rnd++;
      --size;
    }

    // update state: v0, v1 = v0 ^ hmac(v, v1), v0 ^ v1
    clHMAC_SHA256_init(&hmac, ctx->v, sizeof(ctx->v));
    clHMAC_update(&hmac, &ctx->v[clSHA256_DIGEST_SIZE],
                     clSHA256_DIGEST_SIZE);
    digest = clHMAC_final(&hmac);
    for (i = 0; i < clSHA256_DIGEST_SIZE; ++i) {
      ctx->v[clSHA256_DIGEST_SIZE + i] ^= ctx->v[i];
      ctx->v[i] ^= digest[i];
    }
  }
}
