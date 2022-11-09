// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/lib/fcrypto/hkdf.h"

#include <stddef.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/digest.h"
#include "src/security/lib/fcrypto/test/utils.h"

namespace crypto {
namespace testing {
namespace {

TEST(HKDF, Init) {
  size_t md_size;
  ASSERT_OK(digest::GetDigestLen(digest::kSHA256, &md_size));

  Secret ikm;
  Bytes salt;
  ASSERT_OK(ikm.Generate(md_size));
  ASSERT_OK(salt.Randomize(BLOCK_GUID_LEN));

  // Bad version
  HKDF hkdf;
  EXPECT_STATUS(hkdf.Init(digest::kUninitialized, ikm, salt), ZX_ERR_INVALID_ARGS);

  // Bad input key material
  ASSERT_OK(ikm.Generate(md_size - 1));
  EXPECT_STATUS(hkdf.Init(digest::kSHA256, ikm, salt), ZX_ERR_INVALID_ARGS);
  ASSERT_OK(ikm.Generate(md_size));

  // Salt is optional
  ASSERT_OK(salt.Resize(0));
  EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt));
  ASSERT_OK(salt.Randomize(BLOCK_GUID_LEN));

  // Invalid flags
  EXPECT_STATUS(hkdf.Init(digest::kSHA256, ikm, salt, 0x8000), ZX_ERR_INVALID_ARGS);

  // Valid
  EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt));
}

TEST(HKDF, Derive) {
  size_t md_size;
  ASSERT_OK(digest::GetDigestLen(digest::kSHA256, &md_size));

  HKDF hkdf;
  Secret ikm, key1, key2, key3;
  Bytes salt;
  ASSERT_OK(ikm.Generate(md_size));
  ASSERT_OK(salt.Randomize(BLOCK_GUID_LEN));

  // Uninitialized
  EXPECT_STATUS(hkdf.Derive("init", md_size, &key1), ZX_ERR_INVALID_ARGS);
  ASSERT_OK(hkdf.Init(digest::kSHA256, ikm, salt));

  // Label is optional
  EXPECT_OK(hkdf.Derive(nullptr, md_size, &key1));
  EXPECT_OK(hkdf.Derive("", md_size, &key1));

  // Same label, same key
  EXPECT_OK(hkdf.Derive("same", md_size, &key1));
  EXPECT_OK(hkdf.Derive("same", md_size, &key2));
  EXPECT_EQ(key1.len(), key2.len());
  EXPECT_EQ(memcmp(key1.get(), key2.get(), key1.len()), 0);

  // Different label, different key.
  EXPECT_OK(hkdf.Derive("diff", md_size, &key3));
  EXPECT_EQ(key1.len(), key3.len());
  EXPECT_NE(memcmp(key1.get(), key3.get(), key1.len()), 0);
}

// Based on RFC 5869, Appendix A.2: Test with SHA-256 and longer inputs/outputs
TEST(HKDF, Rfc5869_TC1) {
  HKDF hkdf;
  Secret ikm, okm;
  Bytes salt;
  uint8_t* buf;
  ASSERT_OK(ikm.Allocate(22, &buf));
  memset(buf, 0xb, ikm.len());
  ASSERT_OK(salt.Resize(13));
  for (uint8_t i = 0; i < salt.len(); ++i) {
    salt[i] = i;
  }
  const char* info = "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9";
  uint8_t expected[42] = {
      0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36,
      0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56,
      0xec, 0xc4, 0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65,
  };
  EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt, HKDF::ALLOW_WEAK_KEY));
  EXPECT_OK(hkdf.Derive(info, sizeof(expected), &okm));
  EXPECT_EQ(memcmp(okm.get(), expected, sizeof(expected)), 0);
}

// Based on RFC 5869, Appendix A.2: Basic test case with SHA-256
TEST(HKDF, Rfc5869_TC2) {
  HKDF hkdf;
  Secret ikm, okm;
  Bytes salt;
  uint8_t* buf;
  ASSERT_OK(ikm.Allocate(80, &buf));
  for (uint8_t i = 0; i < ikm.len(); ++i) {
    buf[i] = i;
  }
  ASSERT_OK(salt.Resize(80));
  for (uint8_t i = 0; i < salt.len(); ++i) {
    salt[i] = static_cast<uint8_t>(0x60 + i);
  }
  const char* info =
      "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
      "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
      "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
      "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
      "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff";
  uint8_t expected[82] = {
      0xb1, 0x1e, 0x39, 0x8d, 0xc8, 0x03, 0x27, 0xa1, 0xc8, 0xe7, 0xf7, 0x8c, 0x59, 0x6a,
      0x49, 0x34, 0x4f, 0x01, 0x2e, 0xda, 0x2d, 0x4e, 0xfa, 0xd8, 0xa0, 0x50, 0xcc, 0x4c,
      0x19, 0xaf, 0xa9, 0x7c, 0x59, 0x04, 0x5a, 0x99, 0xca, 0xc7, 0x82, 0x72, 0x71, 0xcb,
      0x41, 0xc6, 0x5e, 0x59, 0x0e, 0x09, 0xda, 0x32, 0x75, 0x60, 0x0c, 0x2f, 0x09, 0xb8,
      0x36, 0x77, 0x93, 0xa9, 0xac, 0xa3, 0xdb, 0x71, 0xcc, 0x30, 0xc5, 0x81, 0x79, 0xec,
      0x3e, 0x87, 0xc1, 0x4c, 0x01, 0xd5, 0xc1, 0xf3, 0x43, 0x4f, 0x1d, 0x87,
  };
  EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt));
  EXPECT_OK(hkdf.Derive(info, sizeof(expected), &okm));
  EXPECT_EQ(memcmp(okm.get(), expected, sizeof(expected)), 0);
}

// Based on RFC 5869, Appendix A.3: Test with SHA-256 and zero-length salt/info
TEST(HKDF, Rfc5869_TC3) {
  HKDF hkdf;
  Secret ikm, okm;
  Bytes salt;
  uint8_t* buf;
  ASSERT_OK(ikm.Allocate(22, &buf));
  memset(buf, 0xb, ikm.len());
  const char* info = "";
  uint8_t expected[42] = {
      0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f, 0x71, 0x5f, 0x80, 0x2a, 0x06, 0x3c,
      0x5a, 0x31, 0xb8, 0xa1, 0x1f, 0x5c, 0x5e, 0xe1, 0x87, 0x9e, 0xc3, 0x45, 0x4e, 0x5f,
      0x3c, 0x73, 0x8d, 0x2d, 0x9d, 0x20, 0x13, 0x95, 0xfa, 0xa4, 0xb6, 0x1a, 0x96, 0xc8,
  };
  EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt, HKDF::ALLOW_WEAK_KEY));
  EXPECT_OK(hkdf.Derive(info, sizeof(expected), &okm));
  EXPECT_EQ(memcmp(okm.get(), expected, sizeof(expected)), 0);
}

}  // namespace
}  // namespace testing
}  // namespace crypto
