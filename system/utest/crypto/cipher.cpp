// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "utils.h"

namespace crypto {
namespace testing {
namespace {

// See utils.h; the following macros allow reusing tests for each of the supported Ciphers.
#define EACH_PARAM(OP, Test)                                                                       \
    OP(Test, Cipher, AES128_CTR)                                                                   \
    OP(Test, Cipher, AES256_XTS)

bool TestGetLengths_Uninitialized(void) {
    BEGIN_TEST;
    size_t len;
    EXPECT_ZX(Cipher::GetKeyLen(Cipher::kUninitialized, &len), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(Cipher::GetIVLen(Cipher::kUninitialized, &len), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(Cipher::GetIVLen(Cipher::kUninitialized, &len), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

bool TestGetLengths_AES128_CTR(void) {
    BEGIN_TEST;
    size_t key_len;
    EXPECT_ZX(Cipher::GetKeyLen(Cipher::kAES128_CTR, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(Cipher::GetKeyLen(Cipher::kAES128_CTR, &key_len));
    EXPECT_EQ(key_len, 16U);

    size_t iv_len;
    EXPECT_ZX(Cipher::GetIVLen(Cipher::kAES128_CTR, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(Cipher::GetIVLen(Cipher::kAES128_CTR, &iv_len));
    EXPECT_EQ(iv_len, 16U);

    size_t block_size;
    EXPECT_ZX(Cipher::GetIVLen(Cipher::kAES128_CTR, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(Cipher::GetIVLen(Cipher::kAES128_CTR, &block_size));
    EXPECT_EQ(block_size, 16U);
    END_TEST;
}


bool TestGetLengths_AES256_XTS(void) {
    BEGIN_TEST;
    size_t key_len;
    EXPECT_ZX(Cipher::GetKeyLen(Cipher::kAES256_XTS, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(Cipher::GetKeyLen(Cipher::kAES256_XTS, &key_len));
    EXPECT_EQ(key_len, 64U);

    size_t iv_len;
    EXPECT_ZX(Cipher::GetIVLen(Cipher::kAES256_XTS, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(Cipher::GetIVLen(Cipher::kAES256_XTS, &iv_len));
    EXPECT_EQ(iv_len, 16U);

    size_t block_size;
    EXPECT_ZX(Cipher::GetIVLen(Cipher::kAES256_XTS, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(Cipher::GetIVLen(Cipher::kAES256_XTS, &block_size));
    EXPECT_EQ(block_size, 16U);
    END_TEST;
}

bool TestInitEncrypt_Uninitialized(void) {
    BEGIN_TEST;
    Cipher cipher;
    Bytes key, iv;
    EXPECT_ZX(cipher.InitEncrypt(Cipher::kUninitialized, key, iv), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

bool TestInitEncrypt(Cipher::Algorithm cipher) {
    BEGIN_TEST;
    Cipher encrypt;
    Bytes key, iv;
    ASSERT_OK(GenerateKeyMaterial(cipher, &key, &iv));

    // Bad key
    Bytes bad_key;
    ASSERT_OK(bad_key.Copy(key.get(), key.len() - 1));
    EXPECT_ZX(encrypt.InitEncrypt(cipher, bad_key, iv), ZX_ERR_INVALID_ARGS);

    // Bad IV
    Bytes bad_iv;
    ASSERT_OK(bad_iv.Copy(iv.get(), iv.len() - 1));
    EXPECT_ZX(encrypt.InitEncrypt(cipher, key, bad_iv), ZX_ERR_INVALID_ARGS);

    // Bad alignment
    EXPECT_ZX(encrypt.InitEncrypt(cipher, key, iv, PAGE_SIZE - 1), ZX_ERR_INVALID_ARGS);

    // Valid with and without alignment
    EXPECT_OK(encrypt.InitEncrypt(cipher, key, iv));
    EXPECT_OK(encrypt.InitEncrypt(cipher, key, iv, PAGE_SIZE));

    END_TEST;
}
DEFINE_EACH(TestInitEncrypt)

bool TestInitDecrypt_Uninitialized(void) {
    BEGIN_TEST;
    Cipher decrypt;
    Bytes key, iv;
    EXPECT_ZX(decrypt.InitDecrypt(Cipher::kUninitialized, key, iv), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

bool TestInitDecrypt(Cipher::Algorithm cipher) {
    BEGIN_TEST;
    Cipher decrypt;
    Bytes key, iv;
    ASSERT_OK(GenerateKeyMaterial(cipher, &key, &iv));

    // Bad key
    Bytes bad_key;
    ASSERT_OK(bad_key.Copy(key.get(), key.len() - 1));
    EXPECT_ZX(decrypt.InitDecrypt(cipher, bad_key, iv), ZX_ERR_INVALID_ARGS);

    // Bad IV
    Bytes bad_iv;
    ASSERT_OK(bad_iv.Copy(iv.get(), iv.len() - 1));
    EXPECT_ZX(decrypt.InitDecrypt(cipher, key, bad_iv), ZX_ERR_INVALID_ARGS);

    // Bad alignment
    EXPECT_ZX(decrypt.InitDecrypt(cipher, key, iv, PAGE_SIZE - 1), ZX_ERR_INVALID_ARGS);

    // Valid with and without tweak
    EXPECT_OK(decrypt.InitDecrypt(cipher, key, iv));
    EXPECT_OK(decrypt.InitDecrypt(cipher, key, iv, PAGE_SIZE));

    END_TEST;
}
DEFINE_EACH(TestInitDecrypt)

bool TestEncryptStream(Cipher::Algorithm cipher) {
    BEGIN_TEST;
    size_t len = PAGE_SIZE;
    Bytes key, iv, ptext;
    ASSERT_OK(GenerateKeyMaterial(cipher, &key, &iv));
    ASSERT_OK(ptext.InitRandom(len));
    uint8_t ctext[len];

    // Not initialized
    Cipher encrypt;
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), len, ctext), ZX_ERR_BAD_STATE);
    ASSERT_OK(encrypt.InitEncrypt(cipher, key, iv));

    // Zero length
    EXPECT_OK(encrypt.Encrypt(ptext.get(), 0, ctext));

    // Bad texts
    EXPECT_ZX(encrypt.Encrypt(nullptr, len, ctext), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), len, nullptr), ZX_ERR_INVALID_ARGS);

    // Wrong mode
    EXPECT_ZX(encrypt.Decrypt(ptext.get(), len, ctext), ZX_ERR_BAD_STATE);

    // Valid
    EXPECT_OK(encrypt.Encrypt(ptext.get(), len, ctext));

    // Reset
    encrypt.Reset();
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), len, ctext), ZX_ERR_BAD_STATE);

    END_TEST;
}
DEFINE_EACH(TestEncryptStream)

bool TestEncryptRandomAccess(Cipher::Algorithm cipher) {
    BEGIN_TEST;
    size_t len = PAGE_SIZE;
    Bytes key, iv, ptext;
    ASSERT_OK(GenerateKeyMaterial(cipher, &key, &iv));
    ASSERT_OK(ptext.InitRandom(len));
    uint8_t ctext[len];

    // Not initialized
    Cipher encrypt;
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), len, ctext), ZX_ERR_BAD_STATE);
    ASSERT_OK(encrypt.InitEncrypt(cipher, key, iv, len));

    // Zero length
    EXPECT_OK(encrypt.Encrypt(ptext.get(), 0, 0, ctext));

    // Bad texts
    EXPECT_ZX(encrypt.Encrypt(nullptr, 0, len, ctext), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), 0, len, nullptr), ZX_ERR_INVALID_ARGS);

    // Wrong mode
    EXPECT_ZX(encrypt.Decrypt(ptext.get(), 0, len, ctext), ZX_ERR_BAD_STATE);

    // Bad offset
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), 1, len, ctext), ZX_ERR_INVALID_ARGS);

    // Valid
    EXPECT_OK(encrypt.Encrypt(ptext.get(), len, len, ctext));

    // Reset
    encrypt.Reset();
    EXPECT_ZX(encrypt.Encrypt(ptext.get(), len, ctext), ZX_ERR_BAD_STATE);

    END_TEST;
}
DEFINE_EACH(TestEncryptRandomAccess)

bool TestDecryptStream(Cipher::Algorithm cipher) {
    BEGIN_TEST;
    size_t len = PAGE_SIZE;
    Bytes key, iv, ptext;
    ASSERT_OK(GenerateKeyMaterial(cipher, &key, &iv));
    ASSERT_OK(ptext.InitRandom(len));
    uint8_t ctext[len];
    uint8_t result[len];
    Cipher encrypt;
    ASSERT_OK(encrypt.InitEncrypt(cipher, key, iv));
    ASSERT_OK(encrypt.Encrypt(ptext.get(), len, ctext));

    // Not initialized
    Cipher decrypt;
    EXPECT_ZX(decrypt.Decrypt(ctext, len, result), ZX_ERR_BAD_STATE);
    ASSERT_OK(decrypt.InitDecrypt(cipher, key, iv));

    // Zero length
    EXPECT_OK(decrypt.Decrypt(ctext, 0, result));

    // Bad texts
    EXPECT_ZX(decrypt.Decrypt(nullptr, len, result), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(decrypt.Decrypt(ctext, len, nullptr), ZX_ERR_INVALID_ARGS);

    // Wrong mode
    EXPECT_ZX(decrypt.Encrypt(ctext, len, result), ZX_ERR_BAD_STATE);

    // Valid
    EXPECT_OK(decrypt.Decrypt(ctext, len, result));
    EXPECT_EQ(memcmp(ptext.get(), result, len), 0);

    // Mismatched key, iv
    Bytes bad_key, bad_iv;
    ASSERT_OK(GenerateKeyMaterial(cipher, &bad_key, &bad_iv));

    ASSERT_OK(decrypt.InitDecrypt(cipher, bad_key, iv));
    EXPECT_OK(decrypt.Decrypt(ctext, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    ASSERT_OK(decrypt.InitDecrypt(cipher, key, bad_iv));
    EXPECT_OK(decrypt.Decrypt(ctext, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    // Bad stream order
    ASSERT_OK(decrypt.InitDecrypt(cipher, key, iv));
    EXPECT_OK(decrypt.Decrypt(ctext, len / 2, result + (len / 2)));
    EXPECT_OK(decrypt.Decrypt(ctext + (len / 2), len / 2, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    // Modified
    ctext[0] ^= 1;
    ASSERT_OK(decrypt.InitDecrypt(cipher, key, iv));
    EXPECT_OK(decrypt.Decrypt(ctext, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    // Reset
    decrypt.Reset();
    EXPECT_ZX(decrypt.Decrypt(ctext, len, result), ZX_ERR_BAD_STATE);

    END_TEST;
}
DEFINE_EACH(TestDecryptStream)

bool TestDecryptRandomAccess(Cipher::Algorithm cipher) {
    BEGIN_TEST;
    size_t len = PAGE_SIZE;
    Bytes key, iv, ptext;
    ASSERT_OK(GenerateKeyMaterial(cipher, &key, &iv));
    ASSERT_OK(ptext.InitRandom(len));
    uint8_t ctext[len];
    uint8_t result[len];
    Cipher encrypt;
    ASSERT_OK(encrypt.InitEncrypt(cipher, key, iv, len / 4));
    ASSERT_OK(encrypt.Encrypt(ptext.get(), len, len, ctext));

    // Not initialized
    Cipher decrypt;
    EXPECT_ZX(decrypt.Decrypt(ctext, 0, len, result), ZX_ERR_BAD_STATE);
    ASSERT_OK(decrypt.InitDecrypt(cipher, key, iv, len / 4));

    // Zero length
    EXPECT_OK(decrypt.Decrypt(ctext, 0, 0, result));

    // Bad texts
    EXPECT_ZX(decrypt.Decrypt(nullptr, 0, len, result), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(decrypt.Decrypt(ctext, 0, len, nullptr), ZX_ERR_INVALID_ARGS);

    // Wrong mode
    EXPECT_ZX(decrypt.Encrypt(ctext, 0, len, result), ZX_ERR_BAD_STATE);

    // Bad offset
    EXPECT_ZX(decrypt.Decrypt(ctext, 1, len, result), ZX_ERR_INVALID_ARGS);

    // Valid
    EXPECT_OK(decrypt.Decrypt(ctext, len, len, result));
    EXPECT_EQ(memcmp(ptext.get(), result, len), 0);

    // Mismatched key, iv and offset
    Bytes bad_key, bad_iv;
    ASSERT_OK(GenerateKeyMaterial(cipher, &bad_key, &bad_iv));

    ASSERT_OK(decrypt.InitDecrypt(cipher, bad_key, iv, len / 4));
    EXPECT_OK(decrypt.Decrypt(ctext, len, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    ASSERT_OK(decrypt.InitDecrypt(cipher, key, bad_iv, len / 4));
    EXPECT_OK(decrypt.Decrypt(ctext, len, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    ASSERT_OK(decrypt.InitDecrypt(cipher, key, bad_iv, len / 4));
    EXPECT_OK(decrypt.Decrypt(ctext, 0, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    // Modified
    ctext[0] ^= 1;
    ASSERT_OK(decrypt.InitDecrypt(cipher, key, iv, len / 4));
    EXPECT_OK(decrypt.Decrypt(ctext, len, len, result));
    EXPECT_NE(memcmp(ptext.get(), result, len), 0);

    // Reset
    decrypt.Reset();
    EXPECT_ZX(decrypt.Decrypt(ctext, 0, len, result), ZX_ERR_BAD_STATE);
    END_TEST;
}
DEFINE_EACH(TestDecryptRandomAccess)

// The following tests are taken from NIST's SP 800-38E.  The non-byte aligned tests vectors are
// omitted; as they are not supported.  Of those remaining, every tenth is selected up to number 200
// as a representative sample.
bool TestSP800_TC(Cipher::Algorithm cipher, const char* xkey, const char* xiv, const char* xptext,
                  const char* xctext) {
    BEGIN_TEST;
    Bytes key, iv, ctext, ptext;
    ASSERT_OK(HexToBytes(xkey, &key));
    ASSERT_OK(HexToBytes(xiv, &iv));
    ASSERT_OK(HexToBytes(xptext, &ptext));
    ASSERT_OK(HexToBytes(xctext, &ctext));
    size_t len = ctext.len();
    uint8_t tmp[len];

    Cipher encrypt;
    EXPECT_OK(encrypt.InitEncrypt(cipher, key, iv));
    EXPECT_OK(encrypt.Encrypt(ptext.get(), len, tmp));
    EXPECT_EQ(memcmp(tmp, ctext.get(), len), 0);

    Cipher decrypt;
    EXPECT_OK(decrypt.InitDecrypt(cipher, key, iv));
    EXPECT_OK(decrypt.Decrypt(ctext.get(), len, tmp));
    EXPECT_EQ(memcmp(tmp, ptext.get(), len), 0);
    END_TEST;
}

// clang-format off

// See https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf
bool TestSP800_38A_F5(void) {
    return TestSP800_TC(Cipher::kAES128_CTR,
        // key
        "2b7e151628aed2a6abf7158809cf4f3c",
        // iv
        "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
        // ptext
        "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",
        // ctext
        "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee");
}

// See https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/aes/XTSTestVectors.zip
bool TestSP800_38E_TC010(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "5d4240766e71216ab73da19ea88027488759c3c83aad8223bcb60ad5559f913d1fa858154fbb8217c04ca352b22e492cf9ea81d1a87838125c90a1340d04f8cf",
        // iv
        "08496af5e9e51e06e562ad121ed422e4",
        // ptext
        "ab5ead893b99dc72e927c82edf40c3e9617c6789d9d488d63a91ed7d37892eba",
        // ctext
        "a8fb3a8bb9c1158d08610636137db4bc2adf2907291e965efe91e5b804c2f3f8");
}

bool TestSP800_38E_TC020(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "c9032290ea6c1b8fe8448fdb6e7e48ef0d81c1a0bc84a9052e40807e515733ed93e55838a88ff1c78509c62afb26d52a8ff687846601b0930771e6df1d1f3c4d",
        // iv
        "30ffaecc5c0843078b13d370d912ede9",
        // ptext
        "a02ffe56131167a1b12136f04bb71786aade3b06adf578fd8d998e39a9846c12",
        // ctext
        "5ab207394fc7a0728a2c683a880d4daee8c20553d91722816a76340e2b4e6662");
}

bool TestSP800_38E_TC030(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "d9dd2f932b39b82c666352b104b15b31f714cde49d9d2e019aa1e73db3818b8eddaf4f47f6f1fc173eec2e0c30674803de8780f945d8005d9fe995785912354b",
        // iv
        "dfc989f8d81871a2bfe7839b94dc8a9f",
        // ptext
        "72660b85b4cb16ed7334404fa39877b62a15ebdee777bd1013df9f6733372b62",
        // ctext
        "ae4dd2851a8c12efc5a49cfcb7d98f6eb3a8b6d76400aaf53ca6c7fe142a6689");
}

bool TestSP800_38E_TC040(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "d77104e5756ca260c3c5912439b7f8c81716d5c4a457e24d104ae50b40167a80ff03e0682729d824dfa8c84c794b80303dc9ff0585088ee6532565bec63ad7c2",
        // iv
        "e9dc846cef4a2c41b4a020f44c233f47",
        // ptext
        "c125edd5ff5eaf875cc4b2bba5fb7dc47a2a1dbe5cba38b213372188890f153f",
        // ctext
        "cada4e269a208e1ee4b3379a4ede5dea049a93fd8e0f5b26069800b0789a0319");
}

bool TestSP800_38E_TC050(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "0406cefa3e16325e0b820591b5d45bbf21164b521ded97628835f2d3be7ecca18d1ba0e5d47f10b969420f59c02e731161a2a262b55b5f35f6f8ef365159f50d",
        // iv
        "9ab2ef46133b547a8ab880e17000aba1",
        // ptext
        "cfe237a9399d58034a6ca7f0066a96374235c1659ca7e7fc978a1db2cb30263a",
        // ctext
        "d2f5bfe75ba30148aaf42b56e264e1827f29b8097f06322d4c7c74bcb2ff540c");
}

bool TestSP800_38E_TC060(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "bade4d98d950bc1c0f9af6c0449df05955ad9db136fdab98b07f359b3a3781d44ccd04a9bdbf2191099dd74705811c9cbf26173dba5ca9c1c89566f061d0c943",
        // iv
        "28b0fe036e623143923e8bbc34588269",
        // ptext
        "70ccd34838671d1699a89e113edcae8fd312415b6f8fd5d00b02705887822497",
        // ctext
        "b090dcd79bfc77f1a5ed3470dca309d018c1c82b39832a2e4f355e43a787f643");
}

bool TestSP800_38E_TC070(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "b353e17f495d6b6a24357a6a6c30372d8e6d79923f0e0b62224af47240123ed909f5a94a299a0cbda4ba99e864698803101507e7027041fe04eed90336d89c76",
        // iv
        "45c9b9a9842445dd369f2f9408c76813",
        // ptext
        "d265b71fb89677540d73c441368299c4162e9f5c070c3856813245f0ed402fab",
        // ctext
        "48126086975de6b282a5acdbeec5777e5f5955d7f938f3c56fe69a91b8b63401");
}

bool TestSP800_38E_TC080(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "2c7ca38b0445ca7345c53d05e433a84e93617723ec4f22a9e3c4d822fdda9a88748d653b83ea170668fae1b22525afd3aa78e1f09106a22d640d853524a80b5a",
        // iv
        "44ddb8c843750a34a598c4b1f91d9230",
        // ptext
        "847417b53e2fe4662e6197409e869dcc16c3b1902cbcd5ed01b5b60d890c4f79",
        // ctext
        "187d4fd73abe3cb652f58a3860e0d8ca36509b10b5843c693160a18824b45ff3");
}

bool TestSP800_38E_TC090(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "9051e843a1a216c0bbad5d67e2e30ee414ae0ec29675deca56507ef011ba7f3f273edd58ea12c02ad03ebe2405702a0b8ac33016d216e22af0f1141998ea488b",
        // iv
        "b4599993e7c9b1c96a49828ad0eb0d24",
        // ptext
        "dd09c1fc4932cbebdcc02fd5ae3cd84c69cceaebca4fecfc6f975ca211fbe205",
        // ctext
        "a818957f0e23cdd3b9d579ba40997a9be651566996f656be257a806a36c2756f");
}

bool TestSP800_38E_TC100(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "198363340a2c104edecef6ada540a9c3a752c4fdcab8d16fff1823d98d959389b92bfd43a9df083600e07f712d6f04a20456d452ec6cb7e836da36581ff7ea33",
        // iv
        "3738f1d2fa33ed4fd3dc8345a77c3195",
        // ptext
        "8e9369480e1c33bd5e4f9355cc81acc0a97bac373ab8a292874fe7103b16ed95",
        // ctext
        "3a23189b53f33da3976c3db3a945cbe89b7cbae84f00dc691b4a113ebefe65b2");
}

bool TestSP800_38E_TC110(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "6b1984c24e7eb6628e3a11c9ccd2594033a3a0d9016eae65c2f24e09b9a66e9fe9d163a506dfbccf2d93e8991e2fc560e10435b890b5889a5003e4bf817dc3e0",
        // iv
        "6bb0d3ae4fa86e431619e407d59ad4f4",
        // ptext
        "6a741a945bfbf0c67afd43ba1f841816c099515805d0fc1f7dbf6de900e0aa7a219c88563271b009d1ac90eb7dc99735",
        // ctext
        "e47bce292baa63bef316f680a5f480a7b883dfab6ed5a57f7e29ecb89e354a31c9b174c4abad6cbababa19140c4620a3");
}

bool TestSP800_38E_TC120(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "10c2e0ee117d7c83bcc1422b1c75445fdca94530eac7c9292e76a2766766368d7f852c6f6b25e8448e9e315b761dcb1a31290b643535a66de5c2bc1e5e3978f7",
        // iv
        "5879b20b8e420dbc2258ac2edc8c227a",
        // ptext
        "95fa154a5845e016c11482071cc2078108eca943b4a3a7bdb65c09ebaf4c7b5b529dc5a718b34f488dd8bab912207da9",
        // ctext
        "6edfac840de7a7e9a4718eb8a1270004806bf4d1ef6249c13b482d390cac49e31f8e1bffc387c2837f2c891eb8e1243a");
}

bool TestSP800_38E_TC130(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "43401c1696961f2cfa7f2f825c0bd08683219ef7a3b8f2352c6a27afa14424de31ceb11b0983b981b3cc59f712d7513bbd78b97724544fba99a7370698c1f586",
        // iv
        "78753fb9e9fa3bff92ed0419cddc538b",
        // ptext
        "7b57a6803504864254cf8dc951502410d9cdc6cd2bcc5ba15d8253f42b8f5a6886ac7c7d00c1487012e02c670540e100",
        // ctext
        "99dc0c7a5041257055a6a857ab29191552c63a5432c6371dead034f1167746bfb84c260b304eca8e6ec315dab732e03a");
}

bool TestSP800_38E_TC140(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "1e30686246d41359c6b98bc474ca7c70bfd1b1167183d099443b50050b9abc031d2491249b64dae81532d55e5ec4b8fc0942956b8016e70c05c07c2f9281294a",
        // iv
        "7bf88e00f309e50739b2eb9b8fa8ce07",
        // ptext
        "df6a4358a3aefbf2490a0cf00e7b7be13ed08881003e140a4681bc794a5327f06ac3fb54cb89be10130ee742bc28ba57",
        // ctext
        "fb051d28b1f2d0f225afe2b5738eb3ed30a050642436fd9c65aa3160997204d05efdbb9d0ccda19a497ba135ff0490e4");
}

bool TestSP800_38E_TC150(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "b4713941c6a4ffdbfe2bffdcca09631911e91f1260e650d389803b1aa89f5789fb8ead890218105b63c6d8af1cdaecb8da8c807a16e97ebdab860c169431f596",
        // iv
        "737ec14228a09f9c52041d9dbfcdd013",
        // ptext
        "be7735fe5eed83198698d1597dcfbb5ece39a067a1d0b7486cdf9e80767a55317da178b7ad276974abd4d069604668a6",
        // ctext
        "aef253795f84d13f90706d34d925394c3d9c3bdc06772fded8ee9cd82b407f06482c679672fa4225c8db8f036eb71eb3");
}

bool TestSP800_38E_TC160(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "02f5f16166ff196ecbd88d90ece619f1815e6dcfce2827a407fe1201c4a4c82956318912d9c7a6e12ab2f69e17b83c0ec42fc9abb25629e66c37b8583c2ef9bb",
        // iv
        "bc9ab46c8d61620d078ecd0fe2cc9796",
        // ptext
        "f163a11a1169c6befcf999c68253f24c35bec8416d7bb738309e8f4cdaed4cc4146bddb71388ffe6361c44b30ceb76b4",
        // ctext
        "feaa6a8a357f3427dfb745cd2eebb3bd893efaac50cf6fcee3495f6292257954873dcdfca9bef8cec8f032d7fc378481");
}

bool TestSP800_38E_TC170(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "b67995bece5a587ffdfa9d63ce82700eabaec701312aac591ae4c13045b17832fbffb96fe953be24ad4e22ac146eff566453fb9abec7c80b7d4f849dba96ec2d",
        // iv
        "952d9dbe6d2b70eca8f11a68bd260e46",
        // ptext
        "190e1bd6674eabd5f5954a439c6748c820d036913e6ce075e2c53f3a1c53dca62f99a2377a42ce685b33edb63917b2ff",
        // ctext
        "a458c4a4952c0cd01c096624ffe94f911197691b658f8daee6b1b853775173ded5761e07d9a1a39ef72d8b6242a1422e");
}

bool TestSP800_38E_TC180(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "4324b0fcdcfedc5af7f8170c157ef68680197f5901fb5f3c9b9f85db8319293066a4e1a61c5943865e7b2de129dd3a6db5d8865ac55722399a58822c4e51d0df",
        // iv
        "c0eb880e0ee09b46d3d28ad7b363a851",
        // ptext
        "e6082cecc24808a6b25e7659b24b71e77ec14887750a01fb9d387c2e90acc77243d7a0dbab70e41c34594a4ad197c8aa",
        // ctext
        "d3c5c210afd597feb2e188b0fc08e77992e2e75bd53cd60c507b2ebca37c7b7defadd06500ab67af7c00e5918fca8a16");
}

bool TestSP800_38E_TC190(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "458dfebe5a6e381da894a1551b95467f19fd475be6a61930ea7707c4f21f88cddd7283c59cc4211af68cc4273ab0e31bf24bee161a5690c754f46ee6392eb6fd",
        // iv
        "eb051cfab3db35ff7b3919ede9f79e93",
        // ptext
        "506a8565eb8d3a39e2cc9d32eb477cbbc621d6451e61fcb8528a6b1935071ffb31f18980ef586b97f02e257e09ca5f0b",
        // ctext
        "95943d6d57f17f5d626518cbc2c7175ab97cf74bbfc8571e8100a921061e68df4e193b53e4256356efbc42969ebebee1");
}

bool TestSP800_38E_TC200(void) {
    return TestSP800_TC(Cipher::kAES256_XTS,
        // key
        "28ab33a47b32dbe9ac4e33a7dd3bdea0fc47deae790c3f5c24cc4e97229ce0c0a15160ff5cc544e2b4e03b4ccd55cc685e93e4ddb2fad8879d0774e92780c521",
        // iv
        "3871b04b799f7c572168af16efe880cf",
        // ptext
        "abf99e347e086cad3676dba7d8ad30713de3852514c78db83ad75d75b686bab066f62431cefe3a98de7b713b72c926fc",
        // ctext
        "3501de2f9e6921c2ca6c6f5a7d642e7c6ad6cc1fc8f3ba496fc5ddc6580df5584bfed4bd02e48d898dbd06757b5f5b06");
}
// clang-format on

BEGIN_TEST_CASE(CipherTest)
RUN_TEST(TestGetLengths_Uninitialized)
RUN_EACH(TestGetLengths)
RUN_TEST(TestInitEncrypt_Uninitialized)
RUN_EACH(TestInitEncrypt)
RUN_TEST(TestInitDecrypt_Uninitialized)
RUN_EACH(TestInitDecrypt)
RUN_EACH(TestEncryptStream)
RUN_EACH(TestEncryptRandomAccess)
RUN_EACH(TestDecryptStream)
RUN_EACH(TestDecryptRandomAccess)
RUN_TEST(TestSP800_38A_F5)
RUN_TEST(TestSP800_38E_TC010)
RUN_TEST(TestSP800_38E_TC020)
RUN_TEST(TestSP800_38E_TC030)
RUN_TEST(TestSP800_38E_TC040)
RUN_TEST(TestSP800_38E_TC050)
RUN_TEST(TestSP800_38E_TC060)
RUN_TEST(TestSP800_38E_TC070)
RUN_TEST(TestSP800_38E_TC080)
RUN_TEST(TestSP800_38E_TC090)
RUN_TEST(TestSP800_38E_TC100)
RUN_TEST(TestSP800_38E_TC110)
RUN_TEST(TestSP800_38E_TC120)
RUN_TEST(TestSP800_38E_TC130)
RUN_TEST(TestSP800_38E_TC140)
RUN_TEST(TestSP800_38E_TC150)
RUN_TEST(TestSP800_38E_TC160)
RUN_TEST(TestSP800_38E_TC170)
RUN_TEST(TestSP800_38E_TC180)
RUN_TEST(TestSP800_38E_TC190)
RUN_TEST(TestSP800_38E_TC200)
END_TEST_CASE(CipherTest)

} // namespace
} // namespace testing
} // namespace crypto
