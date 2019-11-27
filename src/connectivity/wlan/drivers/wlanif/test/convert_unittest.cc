// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlanif/convert.h>
#include <wlan/common/element.h>

namespace wlanif {
namespace {
namespace wlan_mlme = ::fuchsia::wlan::mlme;

template <typename T>
zx_status_t ValidateMessage(T* msg) {
  fidl::Encoder enc(0);
  enc.Alloc(fidl::EncodingInlineSize<T>(&enc));
  msg->Encode(&enc, sizeof(fidl_message_header_t));

  auto encoded = enc.GetMessage();
  auto msg_body = encoded.payload();
  const char* err_msg = nullptr;
  return fidl_validate(T::FidlType, msg_body.data(), msg_body.size(), 0, &err_msg);
}

wlanif_bss_description_t FakeBssWithSsidLen(uint8_t ssid_len) {
  return {
      .ssid =
          {
              .len = ssid_len,
          },
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
  };
}

TEST(ConvertTest, ToFidlBSSDescription_SsidEmpty) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(0));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlBSSDescription_Ssid) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(3));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlBSSDescription_SsidMaxLength) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(32));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToFidlBSSDescription_SsidTooLong) {
  wlan_mlme::BSSDescription fidl_desc = {};
  ConvertBSSDescription(&fidl_desc, FakeBssWithSsidLen(33));
  auto status = ValidateMessage(&fidl_desc);
  EXPECT_EQ(status, ZX_OK);
}

TEST(ConvertTest, ToVectorRateSets_InvalidRateCount) {
  wlanif_bss_description bss_desc{};
  std::vector<uint8_t> expected;

  bss_desc.num_rates = WLAN_MAC_MAX_RATES + 1;
  for (unsigned i = 0; i < WLAN_MAC_MAX_RATES + 1; i++) {
    bss_desc.rates[i] = i;
    expected.push_back(i);
  }
  std::vector<uint8_t> rates;

  ConvertRates(&rates, bss_desc);

  expected.resize(WLAN_MAC_MAX_RATES);  // ConvertRates will truncate rates
  EXPECT_EQ(rates, expected);
}

TEST(ConvertTest, ToFidlAssocInd) {
  wlan_mlme::AssociateIndication fidl_ind = {};
  wlanif_assoc_ind_t assoc_ind = {
      .rsne_len = 64,
  };
  // Check if rsne gets copied over
  ConvertAssocInd(&fidl_ind, assoc_ind);
  ASSERT_TRUE(fidl_ind.rsne.has_value());
  ASSERT_TRUE(fidl_ind.rsne->size() == 64);
  auto status = ValidateMessage(&fidl_ind);
  EXPECT_EQ(status, ZX_OK);

  // Check to see rsne is not copied in this case and also ensure
  // the FIDL message gets reset
  assoc_ind.rsne_len = 0;
  ConvertAssocInd(&fidl_ind, assoc_ind);
  ASSERT_FALSE(fidl_ind.rsne.has_value());
  status = ValidateMessage(&fidl_ind);
  EXPECT_EQ(status, ZX_OK);
}
}  // namespace
}  // namespace wlanif
