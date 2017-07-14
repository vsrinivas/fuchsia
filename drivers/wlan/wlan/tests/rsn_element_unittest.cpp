// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_frame.h"
#include "wlan.h"

#include <gtest/gtest.h>

using namespace wlan::cipher_suite_type;
using namespace wlan::akm_suite_type;

namespace wlan {

namespace {

const uint8_t kRsnVersion = 13;

class RsnElementTests : public ::testing::Test {
  protected:
    uint8_t buf_[1024] = {};
    size_t actual_ = 0;
};

TEST_F(RsnElementTests, FullRsn) {
    CipherSuite group_cipher = {.type = kCcmp128};
    std::vector<CipherSuite> pairwise_ciphers_ = {{.type = kWep40}, {.type = kTkip}};
    std::vector<AkmSuite> akm_ciphers_ = {{.type = kPsk}, {.type = k8021X_Ft},
                                          {.type = k8021X_Pmksa}};
    RsnCapabilities caps_;
    caps_.set_mfpc(1);
    caps_.set_peer_key_enabled(1);
    caps_.set_ex_key_id_ind_addr_frames(1);
    std::vector<__uint128_t> pmkids_ = {42, 1337};
    CipherSuite group_mgmt_cipher_ = {.type = kGroupCipherSuite};
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion, &group_cipher,
                                      pairwise_ciphers_, akm_ciphers_, &caps_, pmkids_,
                                      &group_mgmt_cipher_);
    EXPECT_TRUE(success);
    EXPECT_EQ(sizeof(RsnElement) +
              sizeof(CipherSuite) +
              2 + 2 * sizeof(CipherSuite) +
              2 + 3 * sizeof(AkmSuite) +
              sizeof(RsnCapabilities) +
              2 + 2 * sizeof(__uint128_t) +
              sizeof(CipherSuite), actual_);

    auto element = FromBytes<RsnElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(kRsnVersion, element->version);

    auto group_data_cipher_suite = element->group_data_cipher_suite();
    ASSERT_NE(nullptr, group_data_cipher_suite);
    EXPECT_EQ(kCcmp128, group_data_cipher_suite->type);

    auto pairwise_ciphers = element->pairwise_cipher_suite();
    ASSERT_NE(nullptr, pairwise_ciphers);
    ASSERT_EQ(2, pairwise_ciphers->count);
    EXPECT_EQ(kWep40, pairwise_ciphers->list[0].type);
    EXPECT_EQ(kTkip, pairwise_ciphers->list[1].type);

    auto akm_ciphers = element->akm_suite();
    ASSERT_NE(nullptr, akm_ciphers);
    ASSERT_EQ(3, akm_ciphers->count);
    EXPECT_EQ(kPsk, akm_ciphers->list[0].type);
    EXPECT_EQ(k8021X_Ft, akm_ciphers->list[1].type);
    EXPECT_EQ(k8021X_Pmksa, akm_ciphers->list[2].type);

    auto caps = element->rsn_cap();
    ASSERT_NE(nullptr, caps);
    EXPECT_EQ(1, caps->mfpc());
    EXPECT_EQ(1, caps->peer_key_enabled());
    EXPECT_EQ(1, caps->ex_key_id_ind_addr_frames());

    auto pmkids = element->pmkid();
    ASSERT_NE(nullptr, pmkids);
    ASSERT_EQ(2, pmkids->count);
    auto pmkid = reinterpret_cast<uint64_t*>(&pmkids->list[0]);
    EXPECT_EQ(42u, pmkid[0]);
    EXPECT_EQ(0u, pmkid[1]);
    pmkid = reinterpret_cast<uint64_t*>(&pmkids->list[1]);
    EXPECT_EQ(1337u, pmkid[0]);
    EXPECT_EQ(0u, pmkid[1]);

    auto group_mgmt_cipher = element->group_mgmt_cipher_suite();
    ASSERT_NE(nullptr, group_mgmt_cipher);
    EXPECT_EQ(kGroupCipherSuite, group_mgmt_cipher->type);
}

TEST_F(RsnElementTests, ShortRsn) {
    CipherSuite group_cipher = {.type = kCcmp128};
    std::vector<CipherSuite> pairwise_ciphers_ = {{.type = kWep40}, {.type = kTkip}};
    std::vector<AkmSuite> akm_ciphers_ = {{.type = kPsk}, {.type = k8021X_Ft},
                                         {.type = k8021X_Pmksa}};
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion, &group_cipher,
                                      pairwise_ciphers_, akm_ciphers_);
    EXPECT_TRUE(success);
    EXPECT_EQ(sizeof(RsnElement) +
              sizeof(CipherSuite) +
              2 + 2 * sizeof(CipherSuite) +
              2 + 3 * sizeof(AkmSuite), actual_);

    auto element = FromBytes<RsnElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(kRsnVersion, element->version);

    auto group_data_cipher_suite = element->group_data_cipher_suite();
    ASSERT_NE(nullptr, group_data_cipher_suite);
    EXPECT_EQ(kCcmp128, group_data_cipher_suite->type);

    auto pairwise_ciphers = element->pairwise_cipher_suite();
    ASSERT_NE(nullptr, pairwise_ciphers);
    ASSERT_EQ(2, pairwise_ciphers->count);
    EXPECT_EQ(kWep40, pairwise_ciphers->list[0].type);
    EXPECT_EQ(kTkip, pairwise_ciphers->list[1].type);

    auto akm_ciphers = element->akm_suite();
    ASSERT_NE(nullptr, akm_ciphers);
    ASSERT_EQ(3, akm_ciphers->count);
    EXPECT_EQ(kPsk, akm_ciphers->list[0].type);
    EXPECT_EQ(k8021X_Ft, akm_ciphers->list[1].type);
    EXPECT_EQ(k8021X_Pmksa, akm_ciphers->list[2].type);

    ASSERT_EQ(nullptr, element->rsn_cap());
    ASSERT_EQ(nullptr, element->pmkid());
    ASSERT_EQ(nullptr, element->group_mgmt_cipher_suite());
}

TEST_F(RsnElementTests, EmptyRsn) {
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion);
    EXPECT_TRUE(success);
    EXPECT_EQ(sizeof(RsnElement), actual_);

    auto element = FromBytes<RsnElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(kRsnVersion, element->version);
    ASSERT_EQ(nullptr, element->group_data_cipher_suite());
    ASSERT_EQ(nullptr, element->pairwise_cipher_suite());
    ASSERT_EQ(nullptr, element->akm_suite());
    ASSERT_EQ(nullptr, element->rsn_cap());
    ASSERT_EQ(nullptr, element->pmkid());
    ASSERT_EQ(nullptr, element->group_mgmt_cipher_suite());
}

// RSNE which omits the AKM suite but includes subsequent fields such as capabilities.
TEST_F(RsnElementTests, CorruptedRsn) {
    CipherSuite group_cipher;
    std::vector<CipherSuite> pairwise_ciphers_(2);
    std::vector<AkmSuite> akm_ciphers_(0);
    RsnCapabilities caps_;
    std::vector<__uint128_t> pmkids_(2);
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion, &group_cipher,
                                      pairwise_ciphers_, akm_ciphers_, &caps_, pmkids_);

    EXPECT_TRUE(success);
    EXPECT_EQ(sizeof(RsnElement) +
              sizeof(CipherSuite) +
              2 + 2 * sizeof(CipherSuite), actual_);

    auto element = FromBytes<RsnElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);

    auto group_data_cipher_suite = element->group_data_cipher_suite();
    ASSERT_NE(nullptr, group_data_cipher_suite);

    auto pairwise_ciphers = element->pairwise_cipher_suite();
    ASSERT_NE(nullptr, pairwise_ciphers);
    ASSERT_EQ(2, pairwise_ciphers->count);

    ASSERT_EQ(nullptr, element->akm_suite());
    ASSERT_EQ(nullptr, element->rsn_cap());
    ASSERT_EQ(nullptr, element->pmkid());
    ASSERT_EQ(nullptr, element->group_mgmt_cipher_suite());
}

// RSNE which holds more data than its header length allows.
TEST_F(RsnElementTests, TooShortRsn) {
    CipherSuite group_cipher;
    std::vector<CipherSuite> pairwise_ciphers_(2);
    std::vector<AkmSuite> akm_ciphers_(3);
    RsnCapabilities caps_;
    std::vector<__uint128_t> pmkids_(2);
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion, &group_cipher,
                                      pairwise_ciphers_, akm_ciphers_, &caps_, pmkids_);
    EXPECT_TRUE(success);

    // Set header length to include 2 rather than 3 AKM suites.
    auto element = FromBytes<RsnElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    element->hdr.len = sizeof(RsnElement) + sizeof(CipherSuite) +
        2 + 2 * sizeof(CipherSuite) + 2 + sizeof(AkmSuite);

    auto group_data_cipher_suite = element->group_data_cipher_suite();
    ASSERT_NE(nullptr, group_data_cipher_suite);

    auto pairwise_ciphers = element->pairwise_cipher_suite();
    ASSERT_NE(nullptr, pairwise_ciphers);
    ASSERT_EQ(2, pairwise_ciphers->count);

    ASSERT_EQ(nullptr, element->akm_suite());
    ASSERT_EQ(nullptr, element->rsn_cap());
    ASSERT_EQ(nullptr, element->pmkid());
    ASSERT_EQ(nullptr, element->group_mgmt_cipher_suite());
}

// There is an upper limit of 255 ciphers per suite.
TEST_F(RsnElementTests, TooManyCiphersForSuite) {
    CipherSuite group_cipher;
    std::vector<CipherSuite> pairwise_ciphers(300);
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion, &group_cipher,
                                      pairwise_ciphers);
    EXPECT_FALSE(success);
}

// RSNE defines too many ciphers and exceeds maximum size. In tests the maximum size is
// 1024 bytes (in real life 255). The element defines 400 ciphers of 4 bytes each.
TEST_F(RsnElementTests, TooLarge) {
    CipherSuite group_cipher;
    std::vector<CipherSuite> pairwise_ciphers(200);
    std::vector<AkmSuite> akm_ciphers(200);
    auto success = RsnElement::Create(buf_, sizeof(buf_), &actual_, kRsnVersion, &group_cipher,
                                      pairwise_ciphers, akm_ciphers);
    EXPECT_FALSE(success);
}

}  // namespace
}  // namespace wlan
