// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlanif/convert.h>

namespace wlanif {
namespace {
namespace wlan_mlme = ::fuchsia::wlan::mlme;

template <typename T>
zx_status_t ValidateMessage(T* msg) {
  fidl::Encoder enc(0);
  enc.Alloc(fidl::CodingTraits<T>::encoded_size);
  msg->Encode(&enc, sizeof(fidl_message_header_t));

  auto encoded = enc.GetMessage();
  auto msg_body = encoded.payload();
  const char* err_msg = nullptr;
  return fidl_validate(T::FidlType, msg_body.data(), msg_body.size(), 0,
                       &err_msg);
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

}  // namespace
}  // namespace wlanif
