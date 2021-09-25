// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>

#include "log_test.h"

namespace wlan::drivers {

TEST(SsidTest, SsidBytes2StrBasicTest) {
  char ssid[] = "TestSSID";
  std::string out = Log::SsidBytes2Str(reinterpret_cast<uint8_t*>(ssid), strlen(ssid));
  EXPECT_STREQ("5465737453534944", out.c_str());
}

TEST(SsidTest, SsidBytes2StrEmptyTest) {
  char ssid[] = "";
  std::string out = Log::SsidBytes2Str(reinterpret_cast<uint8_t*>(ssid), strlen(ssid));
  EXPECT_STREQ("", out.c_str());
}

TEST(SsidTest, SsidBytes2StrMaxLenTest) {
  constexpr size_t max_ssid_len = ::fuchsia::wlan::ieee80211::MAX_SSID_BYTE_LEN;
  uint8_t ssid[max_ssid_len + 1];

  // We use (2 * max_ssid_len) since each char is represented as a two hex chars.
  constexpr size_t final_len = 2 * max_ssid_len;

  // Expect size of final output to be same when the ssid >= max allowed len.
  EXPECT_EQ(final_len, Log::SsidBytes2Str(ssid, max_ssid_len).length());
  EXPECT_EQ(final_len, Log::SsidBytes2Str(ssid, max_ssid_len + 1).length());

  // Expect size to reduce as we go below max_ssid_len. We reduce by 2 since each char is rep. by 2
  // hex chars.
  EXPECT_EQ(final_len - 2, Log::SsidBytes2Str(ssid, max_ssid_len - 1).length());
}

TEST(SsidTest, MacroTestByte2Str) {
  char ssid[] = "TestSSID";
  EXPECT_STREQ("5465737453534944", FMT_SSID_BYTES(reinterpret_cast<uint8_t*>(ssid), strlen(ssid)));

  char ssid_empty[] = "";
  EXPECT_STREQ("", FMT_SSID_BYTES(reinterpret_cast<uint8_t*>(ssid_empty), strlen(ssid_empty)));
}

TEST(SsidTest, MacroTestVect2Str) {
  std::vector<uint8_t> ssid = {'T', 'e', 's', 't', 'S', 'S', 'I', 'D'};
  EXPECT_STREQ("5465737453534944", FMT_SSID_VECT(ssid));

  std::vector<uint8_t> ssid_empty = {};
  EXPECT_STREQ("", FMT_SSID_VECT(ssid_empty));
}

// FMT_SSID is tied to PII redaction. The diagonistics team needs to be notified of any changes to
// this format.
TEST(SsidText, FMTTest) {
  constexpr char exp_ssid[] = "<ssid-test>";
  char ssid[strlen(exp_ssid) + 1];
  snprintf(ssid, sizeof(ssid), FMT_SSID, "test");
  EXPECT_STREQ(exp_ssid, ssid);
}
}  // namespace wlan::drivers
