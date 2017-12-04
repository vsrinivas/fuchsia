// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <crypto/aead.h>
#include <crypto/bytes.h>
#include <unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "utils.h"

namespace crypto {
namespace testing {
namespace {

// See utils.h; the following macros allow reusing tests for each of the supported AEADs.
#define EACH_PARAM(OP, Test)                                                                       \
    OP(Test, AEAD, AES128_GCM)                                                                     \
    OP(Test, AEAD, AES128_GCM_SIV)

bool TestGetLengths_Uninitialized(void) {
    size_t key_len;
    EXPECT_ZX(AEAD::GetKeyLen(AEAD::kUninitialized, &key_len), ZX_ERR_INVALID_ARGS);

    size_t iv_len;
    EXPECT_ZX(AEAD::GetIVLen(AEAD::kUninitialized, &iv_len), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

bool TestGetLengths_AES128_GCM(void) {
    size_t key_len;
    EXPECT_ZX(AEAD::GetKeyLen(AEAD::kAES128_GCM, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(AEAD::GetKeyLen(AEAD::kAES128_GCM, &key_len));
    EXPECT_EQ(key_len, 16U);

    size_t iv_len;
    EXPECT_ZX(AEAD::GetIVLen(AEAD::kAES128_GCM, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(AEAD::GetIVLen(AEAD::kAES128_GCM, &iv_len));
    EXPECT_EQ(iv_len, 12U);

    size_t tag_len;
    EXPECT_ZX(AEAD::GetTagLen(AEAD::kAES128_GCM, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(AEAD::GetTagLen(AEAD::kAES128_GCM, &tag_len));
    EXPECT_EQ(tag_len, 16U);
    END_TEST;
}

bool TestGetLengths_AES128_GCM_SIV(void) {
    size_t key_len;
    EXPECT_ZX(AEAD::GetKeyLen(AEAD::kAES128_GCM_SIV, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(AEAD::GetKeyLen(AEAD::kAES128_GCM_SIV, &key_len));
    EXPECT_EQ(key_len, 16U);

    size_t iv_len;
    EXPECT_ZX(AEAD::GetIVLen(AEAD::kAES128_GCM_SIV, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(AEAD::GetIVLen(AEAD::kAES128_GCM_SIV, &iv_len));
    EXPECT_EQ(iv_len, 12U);

    size_t tag_len;
    EXPECT_ZX(AEAD::GetTagLen(AEAD::kAES128_GCM_SIV, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(AEAD::GetTagLen(AEAD::kAES128_GCM_SIV, &tag_len));
    EXPECT_EQ(tag_len, 16U);
    END_TEST;
}

bool TestInitSeal_Uninitialized(void) {
    BEGIN_TEST;
    AEAD sealer;
    Bytes key, iv;
    EXPECT_ZX(sealer.InitSeal(AEAD::kUninitialized, key, iv), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

bool TestInitSeal(AEAD::Algorithm aead) {
    BEGIN_TEST;
    AEAD sealer;
    Bytes key, iv;
    ASSERT_OK(GenerateKeyMaterial(aead, &key, &iv));

    // Bad key
    Bytes bad_key;
    ASSERT_OK(bad_key.Copy(key.get(), key.len() - 1));
    EXPECT_ZX(sealer.InitSeal(aead, bad_key, iv), ZX_ERR_INVALID_ARGS);

    // Bad IV
    Bytes bad_iv;
    ASSERT_OK(iv.Copy(iv.get(), iv.len() - 1));
    EXPECT_ZX(sealer.InitSeal(aead, key, bad_iv), ZX_ERR_INVALID_ARGS);

    // Valid
    EXPECT_OK(sealer.InitSeal(aead, key, iv));

    END_TEST;
}
DEFINE_EACH(TestInitSeal)

bool TestInitOpen_Uninitialized(void) {
    BEGIN_TEST;
    AEAD opener;
    Bytes key;
    EXPECT_ZX(opener.InitOpen(AEAD::kUninitialized, key), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

bool TestInitOpen(AEAD::Algorithm aead) {
    BEGIN_TEST;
    AEAD opener;
    Bytes key, iv;
    ASSERT_OK(GenerateKeyMaterial(aead, &key, nullptr));

    // Bad key
    Bytes bad_key;
    ASSERT_OK(bad_key.Copy(key.get(), key.len() - 1));
    EXPECT_ZX(opener.InitOpen(aead, bad_key), ZX_ERR_INVALID_ARGS);

    // Valid
    EXPECT_OK(opener.InitOpen(aead, key));

    END_TEST;
}
DEFINE_EACH(TestInitOpen)

bool TestSealData(AEAD::Algorithm aead) {
    BEGIN_TEST;
    AEAD sealer;
    Bytes key, iv, ptext, ctext;
    ASSERT_OK(GenerateKeyMaterial(aead, &key, &iv));
    ASSERT_OK(ptext.InitRandom(PAGE_SIZE));

    // Not initialized
    EXPECT_ZX(sealer.Seal(ptext, &iv, &ctext), ZX_ERR_BAD_STATE);
    ASSERT_OK(sealer.InitSeal(aead, key, iv));

    // Missing parameters
    EXPECT_ZX(sealer.Seal(ptext, nullptr, &ctext), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(sealer.Seal(ptext, &iv, nullptr), ZX_ERR_INVALID_ARGS);

    // Wrong mode
    EXPECT_ZX(sealer.Open(iv, ctext, &ptext), ZX_ERR_BAD_STATE);

    // Valid
    EXPECT_OK(sealer.Seal(ptext, &iv, &ctext));
    ptext.Reset();
    EXPECT_OK(sealer.Seal(ptext, &iv, &ctext));

    // Reset
    sealer.Reset();
    EXPECT_ZX(sealer.Seal(ptext, &iv, &ctext), ZX_ERR_BAD_STATE);
    END_TEST;
}
DEFINE_EACH(TestSealData)

bool TestOpenData(AEAD::Algorithm aead) {
    BEGIN_TEST;
    Bytes key, iv, ptext, ctext, result;
    ASSERT_OK(GenerateKeyMaterial(aead, &key, &iv));
    ASSERT_OK(ptext.InitRandom(PAGE_SIZE));

    AEAD sealer;
    ASSERT_OK(sealer.InitSeal(aead, key, iv));
    ASSERT_OK(sealer.Seal(ptext, &iv, &ctext));

    // Not initialized
    AEAD opener;
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_BAD_STATE);
    ASSERT_OK(opener.InitOpen(aead, key));

    // Missing parameters
    EXPECT_ZX(opener.Open(iv, ctext, nullptr), ZX_ERR_INVALID_ARGS);

    // Wrong IV
    size_t iv_len = iv.len();
    ASSERT_OK(iv.Resize(iv_len + 1));
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_INVALID_ARGS);
    ASSERT_OK(iv.Resize(iv_len));
    iv[0] ^= 1;
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_IO_DATA_INTEGRITY);
    iv[0] ^= 1;

    // Wrong tag
    size_t len;
    ASSERT_OK(AEAD::GetTagLen(aead, &len));
    ASSERT_OK(ctext.Resize(len - 1));
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_INVALID_ARGS);

    ctext.Reset();
    ASSERT_OK(sealer.Seal(ptext, &iv, &ctext));
    len = ctext.len();
    ctext[len - 1] ^= 1;
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_IO_DATA_INTEGRITY);
    ctext[len - 1] ^= 1;

    // Wrong data
    ctext[0] ^= 1;
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_IO_DATA_INTEGRITY);
    ctext[0] ^= 1;

    // Wrong mode
    EXPECT_ZX(opener.Seal(ptext, &iv, &ctext), ZX_ERR_BAD_STATE);

    // Valid
    ASSERT_OK(sealer.Seal(ptext, &iv, &ctext));
    EXPECT_OK(opener.Open(iv, ctext, &result));

    ASSERT_OK(sealer.Seal(ptext, &iv, &ctext));
    EXPECT_OK(opener.Open(iv, ctext, &result));
    EXPECT_TRUE(ptext == result);

    // Reset
    opener.Reset();
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_BAD_STATE);
    END_TEST;
}
DEFINE_EACH(TestOpenData)

bool TestStaticAD(AEAD::Algorithm aead) {
    BEGIN_TEST;
    Bytes key, iv, ad, ptext, ctext, result;
    ASSERT_OK(GenerateKeyMaterial(aead, &key, &iv));
    ASSERT_OK(ad.InitRandom(16));
    ASSERT_OK(ptext.InitRandom(PAGE_SIZE));

    AEAD sealer, opener;
    ASSERT_OK(sealer.InitSeal(aead, key, iv));
    ASSERT_OK(opener.InitOpen(aead, key));

    // Bad AD
    Bytes ad_seal;
    EXPECT_OK(sealer.SetAD(ad));
    EXPECT_OK(sealer.Seal(ptext, &iv, &ctext));
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_IO_DATA_INTEGRITY);

    // Valid
    Bytes ad_open;
    EXPECT_OK(opener.SetAD(ad));
    EXPECT_OK(opener.Open(iv, ctext, &result));
    EXPECT_TRUE(ptext == result);

    END_TEST;
}
DEFINE_EACH(TestStaticAD)

bool TestDynamicAD(AEAD::Algorithm aead) {
    BEGIN_TEST;
    Bytes key, iv, ptext, ctext, result;
    ASSERT_OK(GenerateKeyMaterial(aead, &key, &iv));
    ASSERT_OK(ptext.InitRandom(PAGE_SIZE));

    AEAD sealer, opener;
    ASSERT_OK(sealer.InitSeal(aead, key, iv));
    ASSERT_OK(opener.InitOpen(aead, key));

    // Bad AD
    uintptr_t p;
    EXPECT_ZX(sealer.AllocAD(1, nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(sealer.AllocAD(0, &p), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(sealer.AllocAD((size_t)-1, &p), ZX_ERR_NO_MEMORY);

    EXPECT_OK(sealer.AllocAD(sizeof(uint64_t), &p));
    uint64_t * ad_seal = reinterpret_cast<uint64_t *>(p);
    EXPECT_OK(opener.AllocAD(sizeof(uint64_t), &p));
    uint64_t *ad_open = reinterpret_cast<uint64_t *>(p);

    // Wrong AD
    *ad_seal = 0;
    EXPECT_OK(sealer.Seal(ptext, &iv, &ctext));
    *ad_open = 1;
    EXPECT_ZX(opener.Open(iv, ctext, &result), ZX_ERR_IO_DATA_INTEGRITY);

    // Valid
    for (uint64_t i = 0; i < 16; ++i) {
        *ad_seal = i;
        *ad_open = i;
        EXPECT_OK(sealer.Seal(ptext, &iv, &ctext));
        EXPECT_OK(opener.Open(iv, ctext, &result));
        EXPECT_TRUE(ptext == result);
    }

    END_TEST;
}
DEFINE_EACH(TestDynamicAD)

// The following tests are taken from NIST's SP 800-38D.  The tests with non-byte non-standard IV
// and tag lengths are omitted.  Of those remaining, the first non-failing test of each combination
// of text and AAD length is selected as a representative sample.
bool TestAes128Gcm_TC(const char* xkey, const char* xiv, const char* xct, const char* xaad,
                      const char* xtag, const char* xpt) {
    BEGIN_TEST;
    Bytes ptext, aad, key, iv, ctext, tag, result;
    ASSERT_OK(HexToBytes(xkey, &key));
    ASSERT_OK(HexToBytes(xiv, &iv));
    ASSERT_OK(HexToBytes(xct, &ctext));
    ASSERT_OK(HexToBytes(xaad, &aad));
    ASSERT_OK(HexToBytes(xtag, &tag));
    ASSERT_OK(HexToBytes(xpt, &ptext));
    ASSERT_OK(ctext.Copy(tag.get(), tag.len(), ctext.len()));

    AEAD sealer;
    EXPECT_OK(sealer.InitSeal(AEAD::kAES128_GCM, key, iv));
    EXPECT_OK(sealer.SetAD(aad));
    EXPECT_OK(sealer.Seal(ptext, &iv, &result));
    EXPECT_TRUE(result == ctext);

    result.Reset();
    AEAD opener;
    EXPECT_OK(opener.InitOpen(AEAD::kAES128_GCM, key));
    EXPECT_OK(opener.SetAD(aad));
    EXPECT_OK(opener.Open(iv, ctext, &result));
    EXPECT_TRUE(result == ptext);
    END_TEST;
}

// clang-format off
bool TestAes128Gcm_TC01(void) {
    return TestAes128Gcm_TC(
        /* Key */ "cf063a34d4a9a76c2c86787d3f96db71",
        /* IV */  "113b9785971864c83b01c787",
        /* CT */  "",
        /* AAD */ "",
        /* Tag */ "72ac8493e3a5228b5d130a69d2510e42",
        /* PT */  "");
}

bool TestAes128Gcm_TC02(void) {
    return TestAes128Gcm_TC(
        /* Key */ "e98b72a9881a84ca6b76e0f43e68647a",
        /* IV */  "8b23299fde174053f3d652ba",
        /* CT */  "5a3c1cf1985dbb8bed818036fdd5ab42",
        /* AAD */ "",
        /* Tag */ "23c7ab0f952b7091cd324835043b5eb5",
        /* PT */  "28286a321293253c3e0aa2704a278032");
}

bool TestAes128Gcm_TC03(void) {
    return TestAes128Gcm_TC(
        /* Key */ "816e39070410cf2184904da03ea5075a",
        /* IV */  "32c367a3362613b27fc3e67e",
        /* CT */  "552ebe012e7bcf90fcef712f8344e8f1",
        /* AAD */ "f2a30728ed874ee02983c294435d3c16",
        /* Tag */ "ecaae9fc68276a45ab0ca3cb9dd9539f",
        /* PT */  "ecafe96c67a1646744f1c891f5e69427");
}

bool TestAes128Gcm_TC04(void) {
    return TestAes128Gcm_TC(
        /* Key */ "d9529840200e1c17725ab52c9c927637",
        /* IV */  "6e9a639d4aecc25530a8ad75",
        /* CT */  "6c779895e78179783c51ade1926436b9",
        /* AAD */ "472a6f4e7771ca391e42065030db3ff418f3b636",
        /* Tag */ "4522bfdef4a635a38db5784b27d43661",
        /* PT */  "8ae823895ee4e7f08bc8bad04d63c220");
}

bool TestAes128Gcm_TC05(void) {
    return TestAes128Gcm_TC(
        /* Key */ "abbc49ee0bbe3d81afc2b6b84f70b748",
        /* IV */  "f11db9f7b99a59ed59ade66f",
        /* CT */  "ce2d76f834942c022044eebc91b461c0",
        /* AAD */ "d533cf7644a48da46fcdec47ae5c77b9b52db775d6c886896e4f4e00c51affd59499a0e572f324989df511c4ea5f93cd",
        /* Tag */ "62df4b04f219554cd3e69d3c870032d2",
        /* PT */  "5135ba1354cbb80478ecaf3db38a443f");
}

bool TestAes128Gcm_TC06(void) {
    return TestAes128Gcm_TC(
        /* Key */ "300b8ffab4368cc90f6d4063e4279f2a",
        /* IV */  "8e69fa64e871d0e98a183a49",
        /* CT */  "2d2292da61c280aff86767d25b75e814",
        /* AAD */ "5166309e153447b27c67051453abf441de3f4a7f6b633ec6122ff82dc132cfb422d36c5ec6e7cc90a9ad55caa1ccdcb82dc5022a20062a9c6e9238f34d085b1f554b5eac05eff25b5a5cb6e18e7827d70175dc0662d77033d118",
        /* Tag */ "633ee657a8981a7682f87505594c95ad",
        /* PT */  "4953b54859870631e818da71fc69c981");
}

bool TestAes128Gcm_TC07(void) {
    return TestAes128Gcm_TC(
        /* Key */ "387218b246c1a8257748b56980e50c94",
        /* IV */  "dd7e014198672be39f95b69d",
        /* CT */  "cdba9e73eaf3d38eceb2b04a8d",
        /* AAD */ "",
        /* Tag */ "ecf90f4a47c9c626d6fb2c765d201556",
        /* PT */  "48f5b426baca03064554cc2b30");
}

bool TestAes128Gcm_TC08(void) {
    return TestAes128Gcm_TC(
        /* Key */ "660eb76f3d8b6ec54e01b8a36263124b",
        /* IV */  "3d8cf16e262880ddfe0c86eb",
        /* CT */  "b1ee05f1415a61d7637e97c5f3",
        /* AAD */ "8560b10c011a1d4190eb46a3692daa17",
        /* Tag */ "761cb84a963e1db1a4ab2c5f904c09db",
        /* PT */  "2efbaedfec3cfe4ac32f201fa5");
}

bool TestAes128Gcm_TC09(void) {
    return TestAes128Gcm_TC(
        /* Key */ "c62dc36b9230e739179f3c58e7270ff9",
        /* IV */  "196a0572d8ff2fbd3522b6a5",
        /* CT */  "958062b331f05b3acaa1836fc2",
        /* AAD */ "4d10536cbdbd6f1d38b2bd10ab8c1c29ae68138e",
        /* Tag */ "dc65a20d9a9aec2e7699eaead47afb42",
        /* PT */  "6d8abcee45667e7a9443896cbf");
}

bool TestAes128Gcm_TC10(void) {
    return TestAes128Gcm_TC(
        /* Key */ "ef1da9dd794219ebf8f717d5a98ab0af",
        /* IV */  "3f3983dc63986e33d1b6bffc",
        /* CT */  "95ea05701481e915c72446c876",
        /* AAD */ "5abd0c1c52b687e9a1673b69137895e5025c2bd18cbeacdb9472e918fe1587da558c492cc708d270fd10572eea83d2de",
        /* Tag */ "5c866992662005ca8886810e278c8ab4",
        /* PT */  "5511872905436c7de38e9501e7");
}

bool TestAes128Gcm_TC11(void) {
    return TestAes128Gcm_TC(
        /* Key */ "77b55a5b37690c9b1b01a05820838e3e",
        /* IV */  "7a8e0d881f023a9954941037",
        /* CT */  "e0eb3359e443e1108ed4068969",
        /* AAD */ "0bb1ad1d294b927c24ee097d0a9afbaa6a62c8923627b50bd96e5ba852509a2e76f7a10ee3987e37a55b92d08531897e6cd76462403b39fb31508cc9fc7684ab5ec2ccc73e8a7f4104a277319bf207fcf263eceed13a76ca177f",
        /* Tag */ "ea6383077d15d7d0a97220848a7616a9",
        /* PT */  "d164aeccec7dbcadee4f41b6a9");
}

bool TestAes128Gcm_TC12(void) {
    return TestAes128Gcm_TC(
        /* Key */ "bfd414a6212958a607a0f5d3ab48471d",
        /* IV */  "86d8ea0ab8e40dcc481cd0e2",
        /* CT */  "62171db33193292d930bf6647347652c1ef33316d7feca99d54f1db4fcf513f8",
        /* AAD */ "",
        /* Tag */ "c28280aa5c6c7a8bd366f28c1cfd1f6e",
        /* PT */  "a6b76a066e63392c9443e60272ceaeb9d25c991b0f2e55e2804e168c05ea591a");
}

bool TestAes128Gcm_TC13(void) {
    return TestAes128Gcm_TC(
        /* Key */ "95bcde70c094f04e3dd8259cafd88ce8",
        /* IV */  "12cf097ad22380432ff40a5c",
        /* CT */  "8a023ba477f5b809bddcda8f55e09064d6d88aaec99c1e141212ea5b08503660",
        /* AAD */ "c783a0cca10a8d9fb8d27d69659463f2",
        /* Tag */ "562f500dae635d60a769b466e15acd1e",
        /* PT */  "32f51e837a9748838925066d69e87180f34a6437e6b396e5643b34cb2ee4f7b1");
}

bool TestAes128Gcm_TC14(void) {
    return TestAes128Gcm_TC(
        /* Key */ "f3e60720c7eff3af96a0e7b2a359c322",
        /* IV */  "8c9cb6af794f8c0fc4c8c06e",
        /* CT */  "73e308d968ead96cefc9337dea6952ac3afbe39d7d14d063b9f59ab89c3f6acc",
        /* AAD */ "5d15b60acc008f9308731ea0a3098644866fa862",
        /* Tag */ "658e311f9c9816dbf2567f811e905ab8",
        /* PT */  "7e299a25404311ee29eee9349f1e7f876dca42ba81f44295bb9b3a152a27a2af");
}

bool TestAes128Gcm_TC15(void) {
    return TestAes128Gcm_TC(
        /* Key */ "8453cf505f22445634b18680c1f6b0f3",
        /* IV */  "fab8e5ce90102286182ef690",
        /* CT */  "5475442af3ba2bd865ae082bc5e92ad7f42cd84b8c64daadcf18f0d4863b6172",
        /* AAD */ "ff76d2210f2caec37490a19352c3945be1c5facb89cb3e9947754cade47ec932d95c88d7d2299a8b6db76b5144ab9516",
        /* Tag */ "972a7e85787ba26c626db1a1e7c13acb",
        /* PT */  "e4abb4773f5cc51c9df6322612d75f70696c17733ce41e22427250ae61fd90d3");
}

bool TestAes128Gcm_TC16(void) {
    return TestAes128Gcm_TC(
        /* Key */ "07a6be880a58f572dbc2ad74a56db8b6",
        /* IV */  "95fc6654e6dc3a8adf5e7a69",
        /* CT */  "095635c7e0eac0fc1059e67e1a936b6f72671121f96699fed520e5f8aff777f0",
        /* AAD */ "de4269feea1a439d6e8990fd6f9f9d5bc67935294425255ea89b6f6772d680fd656b06581a5d8bc5c017ab532b4a9b83a55fde58cdfb3d2a8fef3aa426bc59d3e32f09d3cc20b1ceb9a9e349d1068a0aa3d39617fae0582ccef0",
        /* Tag */ "b2235f6d4bdd7b9c0901711048859d47",
        /* PT */  "7680b48b5d28f38cdeab2d5851769394a3e141b990ec4bdf79a33e5315ac0338");
}

bool TestAes128Gcm_TC17(void) {
    return TestAes128Gcm_TC(
        /* Key */ "93ae114052b7985d409a39a40df8c7ee",
        /* IV */  "8ad733a4a9b8330690238c42",
        /* CT */  "bbb5b672a479afca2b11adb0a4c762b698dd565908fee1d101f6a01d63332c91b85d7f03ac48a477897d512b4572f9042cb7ea",
        /* AAD */ "",
        /* Tag */ "4d78bdcb1366fcba02fdccee57e1ff44",
        /* PT */  "3f3bb0644eac878b97d990d257f5b36e1793490dbc13fea4efe9822cebba7444cce4dee5a7f5dfdf285f96785792812200c279");
}

bool TestAes128Gcm_TC18(void) {
    return TestAes128Gcm_TC(
        /* Key */ "bc22f3f05cc40db9311e4192966fee92",
        /* IV */  "134988e662343c06d3ab83db",
        /* CT */  "4c0168ab95d3a10ef25e5924108389365c67d97778995892d9fd46897384af61fc559212b3267e90fe4df7bfd1fbed46f4b9ee",
        /* AAD */ "10087e6ed81049b509c31d12fee88c64",
        /* Tag */ "771357958a316f166bd0dacc98ea801a",
        /* PT */  "337c1bc992386cf0f957617fe4d5ec1218ae1cc40369305518eb177e9b15c1646b142ff71237efaa58790080cd82e8848b295c");
}

bool TestAes128Gcm_TC19(void) {
    return TestAes128Gcm_TC(
        /* Key */ "af57f42c60c0fc5a09adb81ab86ca1c3",
        /* IV */  "a2dc01871f37025dc0fc9a79",
        /* CT */  "b9a535864f48ea7b6b1367914978f9bfa087d854bb0e269bed8d279d2eea1210e48947338b22f9bad09093276a331e9c79c7f4",
        /* AAD */ "41dc38988945fcb44faf2ef72d0061289ef8efd8",
        /* Tag */ "4f71e72bde0018f555c5adcce062e005",
        /* PT */  "3803a0727eeb0ade441e0ec107161ded2d425ec0d102f21f51bf2cf9947c7ec4aa72795b2f69b041596e8817d0a3c16f8fadeb");
}

bool TestAes128Gcm_TC20(void) {
    return TestAes128Gcm_TC(
        /* Key */ "f0305c7b513960533519473976f02beb",
        /* IV */  "1a7f6ea0e6c9aa5cf8b78b09",
        /* CT */  "30043bcbe2177ab25e4b00a92ee1cd80e9daaea0bc0a827fc5fcb84e7b07be6395582a5a14e768dde80a20dae0a8b1d8d1d29b",
        /* AAD */ "7e2071cc1c70719143981de543cd28dbceb92de0d6021bda4417e7b6417938b126632ecff6e00766e5d0aad3d6f06811",
        /* Tag */ "796c41624f6c3cab762380d21ab6130b",
        /* PT */  "e5fc990c0739e05bd4655871c7401128117737a11d520372239ab723f7fde78dc4212ac565ee5ee100a014dbb71ea13cdb08eb");
}

bool TestAes128Gcm_TC21(void) {
    return TestAes128Gcm_TC(
        /* Key */ "da2bb7d581493d692380c77105590201",
        /* IV */  "44aa3e7856ca279d2eb020c6",
        /* CT */  "9290d430c9e89c37f0446dbd620c9a6b34b1274aeb6f911f75867efcf95b6feda69f1af4ee16c761b3c9aeac3da03aa9889c88",
        /* AAD */ "4cd171b23bddb3a53cdf959d5c1710b481eb3785a90eb20a2345ee00d0bb7868c367ab12e6f4dd1dee72af4eee1d197777d1d6499cc541f34edbf45cda6ef90b3c024f9272d72ec1909fb8fba7db88a4d6f7d3d925980f9f9f72",
        /* Tag */ "9e3ac938d3eb0cadd6f5c9e35d22ba38",
        /* PT */  "9bbf4c1a2742f6ac80cb4e8a052e4a8f4f07c43602361355b717381edf9fabd4cb7e3ad65dbd1378b196ac270588dd0621f642");
}
// clang-format on

// The following tests are taken from the draft AES-GCM RFC (version 6), appendix C.1.  They are
// intentionally formatted to be as close to the RFC's representation as possible.
bool TestAes128GcmSiv_TC(const char* xpt, const char* xaad, const char* xkey, const char* xnonce,
                         const char* xresult) {
    BEGIN_TEST;
    Bytes ptext, aad, key, iv, ctext, tag, result;
    ASSERT_OK(HexToBytes(xpt, &ptext));
    ASSERT_OK(HexToBytes(xaad, &aad));
    ASSERT_OK(HexToBytes(xkey, &key));
    ASSERT_OK(HexToBytes(xnonce, &iv));
    ASSERT_OK(HexToBytes(xresult, &result));

    AEAD sealer;
    EXPECT_OK(sealer.InitSeal(AEAD::kAES128_GCM_SIV, key, iv));
    EXPECT_OK(sealer.SetAD(aad));
    EXPECT_OK(sealer.Seal(ptext, &iv, &ctext));
    EXPECT_TRUE(ctext == result);

    result.Reset();
    AEAD opener;
    EXPECT_OK(opener.InitOpen(AEAD::kAES128_GCM_SIV, key));
    EXPECT_OK(opener.SetAD(aad));
    EXPECT_OK(opener.Open(iv, ctext, &result));
    EXPECT_TRUE(ptext == result);
    END_TEST;
}

// clang-format off
bool TestAes128GcmSiv_TC01(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (0 bytes) */  "",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (16 bytes) */    "dc20e2d83f25705bb49e439eca56de25");
}

bool TestAes128GcmSiv_TC02(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (8 bytes) */  "0100000000000000",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (24 bytes) */    "b5d839330ac7b786578782fff6013b81"
                                   "5b287c22493a364c");
}

bool TestAes128GcmSiv_TC03(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (12 bytes) */ "010000000000000000000000",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (28 bytes) */    "7323ea61d05932260047d942a4978db3"
                                   "57391a0bc4fdec8b0d106639");
}

bool TestAes128GcmSiv_TC04(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (16 bytes) */ "01000000000000000000000000000000",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (32 bytes) */    "743f7c8077ab25f8624e2e948579cf77"
                                   "303aaf90f6fe21199c6068577437a0c4");
}

bool TestAes128GcmSiv_TC05(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (32 bytes) */ "01000000000000000000000000000000"
                                   "02000000000000000000000000000000",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (48 bytes) */    "84e07e62ba83a6585417245d7ec413a9"
                                   "fe427d6315c09b57ce45f2e3936a9445"
                                   "1a8e45dcd4578c667cd86847bf6155ff");
}

bool TestAes128GcmSiv_TC06(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (48 bytes) */ "01000000000000000000000000000000"
                                   "02000000000000000000000000000000"
                                   "03000000000000000000000000000000",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (64 bytes) */    "3fd24ce1f5a67b75bf2351f181a475c7"
                                   "b800a5b4d3dcf70106b1eea82fa1d64d"
                                   "f42bf7226122fa92e17a40eeaac1201b"
                                   "5e6e311dbf395d35b0fe39c2714388f8");
}

bool TestAes128GcmSiv_TC07(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (64 bytes) */ "01000000000000000000000000000000"
                                   "02000000000000000000000000000000"
                                   "03000000000000000000000000000000"
                                   "04000000000000000000000000000000",
        /* AAD (0 bytes) */        "",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (80 bytes) */    "2433668f1058190f6d43e360f4f35cd8"
                                   "e475127cfca7028ea8ab5c20f7ab2af0"
                                   "2516a2bdcbc08d521be37ff28c152bba"
                                   "36697f25b4cd169c6590d1dd39566d3f"
                                   "8a263dd317aa88d56bdf3936dba75bb8");
}

bool TestAes128GcmSiv_TC08(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (8 bytes) */  "0200000000000000",
        /* AAD (1 bytes) */        "01",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (24 bytes) */    "1e6daba35669f4273b0a1a2560969cdf"
                                   "790d99759abd1508");
}

bool TestAes128GcmSiv_TC09(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (12 bytes) */ "020000000000000000000000",
        /* AAD (1 bytes) */        "01",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (28 bytes) */    "296c7889fd99f41917f4462008299c51"
                                   "02745aaa3a0c469fad9e075a");
}

bool TestAes128GcmSiv_TC10(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (16 bytes) */ "02000000000000000000000000000000",
        /* AAD (1 bytes) */        "01",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (32 bytes) */    "e2b0c5da79a901c1745f700525cb335b"
                                   "8f8936ec039e4e4bb97ebd8c4457441f");
}

bool TestAes128GcmSiv_TC11(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (32 bytes) */ "02000000000000000000000000000000"
                                   "03000000000000000000000000000000",
        /* AAD (1 bytes) */        "01",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (48 bytes) */    "620048ef3c1e73e57e02bb8562c416a3"
                                   "19e73e4caac8e96a1ecb2933145a1d71"
                                   "e6af6a7f87287da059a71684ed3498e1");
}

bool TestAes128GcmSiv_TC12(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (48 bytes) */ "02000000000000000000000000000000"
                                   "03000000000000000000000000000000"
                                   "04000000000000000000000000000000",
        /* AAD (1 bytes) */        "01",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (64 bytes) */    "50c8303ea93925d64090d07bd109dfd9"
                                   "515a5a33431019c17d93465999a8b005"
                                   "3201d723120a8562b838cdff25bf9d1e"
                                   "6a8cc3865f76897c2e4b245cf31c51f2");
}

bool TestAes128GcmSiv_TC13(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (64 bytes) */ "02000000000000000000000000000000"
                                   "03000000000000000000000000000000"
                                   "04000000000000000000000000000000"
                                   "05000000000000000000000000000000",
        /* AAD (1 bytes) */        "01",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (80 bytes) */    "2f5c64059db55ee0fb847ed513003746"
                                   "aca4e61c711b5de2e7a77ffd02da42fe"
                                   "ec601910d3467bb8b36ebbaebce5fba3"
                                   "0d36c95f48a3e7980f0e7ac299332a80"
                                   "cdc46ae475563de037001ef84ae21744");
}

bool TestAes128GcmSiv_TC14(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (4 bytes) */  "02000000",
        /* AAD (12 bytes) */       "010000000000000000000000",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (20 bytes) */    "a8fe3e8707eb1f84fb28f8cb73de8e99"
                                   "e2f48a14");
}

bool TestAes128GcmSiv_TC15(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (20 bytes) */ "03000000000000000000000000000000"
                                   "04000000",
        /* AAD (18 bytes) */       "01000000000000000000000000000000"
                                   "0200",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (36 bytes) */    "6bb0fecf5ded9b77f902c7d5da236a43"
                                   "91dd029724afc9805e976f451e6d87f6"
                                   "fe106514");
}

bool TestAes128GcmSiv_TC16(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (18 bytes) */ "03000000000000000000000000000000"
                                   "0400",
        /* AAD (20 bytes) */       "01000000000000000000000000000000"
                                   "02000000",
        /* Key */                  "01000000000000000000000000000000",
        /* Nonce */                "030000000000000000000000",
        /* Result (34 bytes) */    "44d0aaf6fb2f1f34add5e8064e83e12a"
                                   "2adabff9b2ef00fb47920cc72a0c0f13"
                                   "b9fd");
}

bool TestAes128GcmSiv_TC17(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (0 bytes) */  "",
        /* AAD (0 bytes) */        "",
        /* Key */                  "e66021d5eb8e4f4066d4adb9c33560e4",
        /* Nonce */                "f46e44bb3da0015c94f70887",
        /* Result (16 bytes) */    "a4194b79071b01a87d65f706e3949578");
}

bool TestAes128GcmSiv_TC18(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (3 bytes) */  "7a806c",
        /* AAD (5 bytes) */        "46bb91c3c5",
        /* Key */                  "36864200e0eaf5284d884a0e77d31646",
        /* Nonce */                "bae8e37fc83441b16034566b",
        /* Result (19 bytes) */    "af60eb711bd85bc1e4d3e0a462e074ee"
                                   "a428a8");
}

bool TestAes128GcmSiv_TC19(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (6 bytes) */  "bdc66f146545",
        /* AAD (10 bytes) */       "fc880c94a95198874296",
        /* Key */                  "aedb64a6c590bc84d1a5e269e4b47801",
        /* Nonce */                "afc0577e34699b9e671fdd4f",
        /* Result (22 bytes) */    "bb93a3e34d3cd6a9c45545cfc11f03ad"
                                   "743dba20f966");
}

bool TestAes128GcmSiv_TC20(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (9 bytes) */  "1177441f195495860f",
        /* AAD (15 bytes) */       "046787f3ea22c127aaf195d1894728",
        /* Key */                  "d5cc1fd161320b6920ce07787f86743b",
        /* Nonce */                "275d1ab32f6d1f0434d8848c",
        /* Result (25 bytes) */    "4f37281f7ad12949d01d02fd0cd174c8"
                                   "4fc5dae2f60f52fd2b");
}

bool TestAes128GcmSiv_TC21(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (12 bytes) */ "9f572c614b4745914474e7c7",
        /* AAD (20 bytes) */       "c9882e5386fd9f92ec489c8fde2be2cf"
                                   "97e74e93",
        /* Key */                  "b3fed1473c528b8426a582995929a149",
        /* Nonce */                "9e9ad8780c8d63d0ab4149c0",
        /* Result (28 bytes) */    "f54673c5ddf710c745641c8bc1dc2f87"
                                   "1fb7561da1286e655e24b7b0");
}

bool TestAes128GcmSiv_TC22(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (15 bytes) */ "0d8c8451178082355c9e940fea2f58",
        /* AAD (25 bytes) */       "2950a70d5a1db2316fd568378da107b5"
                                   "2b0da55210cc1c1b0a",
        /* Key */                  "2d4ed87da44102952ef94b02b805249b",
        /* Nonce */                "ac80e6f61455bfac8308a2d4",
        /* Result (31 bytes) */    "c9ff545e07b88a015f05b274540aa183"
                                   "b3449b9f39552de99dc214a1190b0b");
}

bool TestAes128GcmSiv_TC23(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (18 bytes) */ "6b3db4da3d57aa94842b9803a96e07fb"
                                   "6de7",
        /* AAD (30 bytes) */       "1860f762ebfbd08284e421702de0de18"
                                   "baa9c9596291b08466f37de21c7f",
        /* Key */                  "bde3b2f204d1e9f8b06bc47f9745b3d1",
        /* Nonce */                "ae06556fb6aa7890bebc18fe",
        /* Result (34 bytes) */    "6298b296e24e8cc35dce0bed484b7f30"
                                   "d5803e377094f04709f64d7b985310a4"
                                   "db84");
}

bool TestAes128GcmSiv_TC24(void) {
    return TestAes128GcmSiv_TC(
        /* Plaintext (21 bytes) */ "e42a3c02c25b64869e146d7b233987bd"
                                   "dfc240871d",
        /* AAD (35 bytes) */       "7576f7028ec6eb5ea7e298342a94d4b2"
                                   "02b370ef9768ec6561c4fe6b7e7296fa"
                                   "859c21",
        /* Key */                  "f901cfe8a69615a93fdf7a98cad48179",
        /* Nonce */                "6245709fb18853f68d833640",
        /* Result (37 bytes) */    "391cc328d484a4f46406181bcd62efd9"
                                   "b3ee197d052d15506c84a9edd65e13e9"
                                   "d24a2a6e70");
}
// clang-format off

BEGIN_TEST_CASE(AeadTest)
RUN_TEST(TestGetLengths_Uninitialized)
RUN_EACH(TestGetLengths)
RUN_TEST(TestInitSeal_Uninitialized)
RUN_EACH(TestInitSeal)
RUN_TEST(TestInitOpen_Uninitialized)
RUN_EACH(TestInitOpen)
RUN_EACH(TestSealData)
RUN_EACH(TestOpenData)
RUN_TEST(TestAes128Gcm_TC01)
RUN_TEST(TestAes128Gcm_TC02)
RUN_TEST(TestAes128Gcm_TC03)
RUN_TEST(TestAes128Gcm_TC04)
RUN_TEST(TestAes128Gcm_TC05)
RUN_TEST(TestAes128Gcm_TC06)
RUN_TEST(TestAes128Gcm_TC07)
RUN_TEST(TestAes128Gcm_TC08)
RUN_TEST(TestAes128Gcm_TC09)
RUN_TEST(TestAes128Gcm_TC10)
RUN_TEST(TestAes128Gcm_TC11)
RUN_TEST(TestAes128Gcm_TC12)
RUN_TEST(TestAes128Gcm_TC13)
RUN_TEST(TestAes128Gcm_TC14)
RUN_TEST(TestAes128Gcm_TC15)
RUN_TEST(TestAes128Gcm_TC16)
RUN_TEST(TestAes128Gcm_TC17)
RUN_TEST(TestAes128Gcm_TC18)
RUN_TEST(TestAes128Gcm_TC19)
RUN_TEST(TestAes128Gcm_TC20)
RUN_TEST(TestAes128Gcm_TC21)
RUN_TEST(TestAes128GcmSiv_TC01)
RUN_TEST(TestAes128GcmSiv_TC02)
RUN_TEST(TestAes128GcmSiv_TC03)
RUN_TEST(TestAes128GcmSiv_TC04)
RUN_TEST(TestAes128GcmSiv_TC05)
RUN_TEST(TestAes128GcmSiv_TC06)
RUN_TEST(TestAes128GcmSiv_TC07)
RUN_TEST(TestAes128GcmSiv_TC08)
RUN_TEST(TestAes128GcmSiv_TC09)
RUN_TEST(TestAes128GcmSiv_TC10)
RUN_TEST(TestAes128GcmSiv_TC11)
RUN_TEST(TestAes128GcmSiv_TC12)
RUN_TEST(TestAes128GcmSiv_TC13)
RUN_TEST(TestAes128GcmSiv_TC14)
RUN_TEST(TestAes128GcmSiv_TC15)
RUN_TEST(TestAes128GcmSiv_TC16)
RUN_TEST(TestAes128GcmSiv_TC17)
RUN_TEST(TestAes128GcmSiv_TC18)
RUN_TEST(TestAes128GcmSiv_TC19)
RUN_TEST(TestAes128GcmSiv_TC20)
RUN_TEST(TestAes128GcmSiv_TC21)
RUN_TEST(TestAes128GcmSiv_TC22)
RUN_TEST(TestAes128GcmSiv_TC23)
RUN_TEST(TestAes128GcmSiv_TC24)
END_TEST_CASE(AeadTest)

} // namespace
} // namespace testing
} // namespace crypto
