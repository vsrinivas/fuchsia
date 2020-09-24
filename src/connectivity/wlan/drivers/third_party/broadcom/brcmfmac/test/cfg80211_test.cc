/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"

#include "gtest/gtest.h"

namespace {

TEST(Cfg80211, ExtractIes) {
  const uint8_t ies[] = {
      0x00, 0x03, 0x66, 0x6f, 0x6f,                                // SSID
      0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,  // Supported rates
      0x32, 0x01, 0x55,  // Extended supported rates. Note: Couldn't find a packet capture with
                         // this IE. Made up a value, so it's probably invalid.
      0x30, 0x02, 0x88, 0x99,  // RSNE (note: invalid, but good enough for testing)

      // Vendor IEs
      0xdd, 0x05, 0x00, 0x50, 0xf2, 0x01, 0xaa,  // WPA (note: invalid, but good enough for testing)
      0xdd, 0x05, 0x11, 0x22, 0x33, 0x44, 0x55,  // Neither WPA nor WSC, so would not be included
      0xdd, 0x05, 0x00, 0x50, 0xf2, 0x04, 0xbb,  // WSC (note: invalid, but good enough for testing)
      0xdd, 0x05, 0x00, 0x50, 0xf2, 0x04, 0xcc   // WSC. We only process one of each type, so this
                                                 // would be excluded.
  };

  wlanif_bss_description_t bss = {};
  brcmf_extract_ies(ies, sizeof(ies), &bss);

  const uint8_t ssid_bytes[] = {0x66, 0x6f, 0x6f};
  ASSERT_EQ(bss.ssid.len, sizeof(ssid_bytes));
  EXPECT_EQ(std::memcmp(bss.ssid.data, ssid_bytes, sizeof(ssid_bytes)), 0);

  const uint8_t rates_bytes[] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c, 0x55};
  ASSERT_EQ(bss.num_rates, sizeof(rates_bytes));  // 8 supported rates, 1 extended supported rate
  EXPECT_EQ(std::memcmp(bss.rates, rates_bytes, sizeof(rates_bytes)), 0);

  const uint8_t rsne_bytes[] = {0x30, 0x02, 0x88, 0x99};
  ASSERT_EQ(bss.rsne_len, sizeof(rsne_bytes));
  EXPECT_EQ(std::memcmp(bss.rsne, rsne_bytes, sizeof(rsne_bytes)), 0);

  const uint8_t vendor_ie_bytes[] = {0xdd, 0x05, 0x00, 0x50, 0xf2, 0x01, 0xaa,
                                     0xdd, 0x05, 0x00, 0x50, 0xf2, 0x04, 0xbb};
  ASSERT_EQ(bss.vendor_ie_len, sizeof(vendor_ie_bytes));
  EXPECT_EQ(std::memcmp(bss.vendor_ie, vendor_ie_bytes, sizeof(vendor_ie_bytes)), 0);
}

TEST(Cfg80211, ExtractIes_WithNoSsidIe) {
  const uint8_t ies[] = {
      0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,  // Supported rates
      0x32, 0x01, 0x55,  // Extended supported rates. Note: Couldn't find a packet capture with
                         // this IE. Made up a value, so it's probably invalid.
      0x30, 0x02, 0x88, 0x99,  // RSNE (note: invalid, but good enough for testing)

      // Vendor IEs
      0xdd, 0x05, 0x00, 0x50, 0xf2, 0x01, 0xaa  // WPA (note: invalid, but good enough for testing)
  };

  wlanif_bss_description_t bss = {};
  brcmf_extract_ies(ies, sizeof(ies), &bss);

  EXPECT_EQ(bss.ssid.len, 0);

  const uint8_t rates_bytes[] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c, 0x55};
  ASSERT_EQ(bss.num_rates, sizeof(rates_bytes));  // 8 supported rates, 1 extended supported rate
  EXPECT_EQ(std::memcmp(bss.rates, rates_bytes, sizeof(rates_bytes)), 0);

  const uint8_t rsne_bytes[] = {0x30, 0x02, 0x88, 0x99};
  ASSERT_EQ(bss.rsne_len, sizeof(rsne_bytes));
  EXPECT_EQ(std::memcmp(bss.rsne, rsne_bytes, sizeof(rsne_bytes)), 0);

  const uint8_t vendor_ie_bytes[] = {0xdd, 0x05, 0x00, 0x50, 0xf2, 0x01, 0xaa};
  ASSERT_EQ(bss.vendor_ie_len, sizeof(vendor_ie_bytes));
  EXPECT_EQ(std::memcmp(bss.vendor_ie, vendor_ie_bytes, sizeof(vendor_ie_bytes)), 0);
}

// Extract the same IE type multiple times and make sure the BSS description changes.
TEST(Cfg80211, ExtractIes_RepeatedIeExtractionOverwrites) {
  // SSID information element.
  const uint8_t first_ssid_ie[] = {0x00, 0x04, 0x66, 0x6f, 0x6f, 0x8};
  const uint8_t first_ssid_bytes[] = {0x66, 0x6f, 0x6f, 0x8};

  wlanif_bss_description_t bss = {};
  brcmf_extract_ies(first_ssid_ie, sizeof(first_ssid_ie), &bss);

  ASSERT_EQ(bss.ssid.len, sizeof(first_ssid_bytes));
  EXPECT_EQ(std::memcmp(bss.ssid.data, first_ssid_bytes, sizeof(first_ssid_bytes)), 0);

  const uint8_t second_ssid_ie[] = {0x00, 0x03, 0x11, 0x7a, 0x9};
  const uint8_t second_ssid_bytes[] = {0x11, 0x7a, 0x9};

  brcmf_extract_ies(second_ssid_ie, sizeof(second_ssid_ie), &bss);

  ASSERT_EQ(bss.ssid.len, sizeof(second_ssid_bytes));
  EXPECT_EQ(std::memcmp(bss.ssid.data, second_ssid_bytes, sizeof(second_ssid_bytes)), 0);
}

TEST(Cfg80211, ExtractIes_ResetVendorIeLength) {
  const uint8_t ies[] = {
      // Vendor IEs
      0xdd, 0x05, 0x00, 0x50, 0xf2, 0x01, 0xaa,  // WPA (note: invalid, but good enough for testing)
  };

  wlanif_bss_description_t bss = {};
  // Test to verify that pre-existing vendor_ie_len value does not impact logic for
  // extracting the IEs.
  bss.vendor_ie_len = 500;

  brcmf_extract_ies(ies, sizeof(ies), &bss);
  uint8_t vendor_ie_bytes[] = {0xdd, 0x05, 0x00, 0x50, 0xf2, 0x01, 0xaa};
  ASSERT_EQ(bss.vendor_ie_len, sizeof(vendor_ie_bytes));
  EXPECT_EQ(std::memcmp(bss.vendor_ie, vendor_ie_bytes, sizeof(vendor_ie_bytes)), 0);
}

TEST(Cfg80211, Classify8021d_Ipv4) {
  uint8_t ip_payload[] = {
      0x01, 0x01,      0x01, 0x01, 0x01, 0x01,  // dst addr
      0x02, 0x02,      0x02, 0x02, 0x02, 0x02,  // src addr
      0x08, 0x00,                               // ipv4 ethertype
      0xff, 0b10110000                          // part of ipv4 header
  };
  uint8_t priority = brcmf_cfg80211_classify8021d(ip_payload, sizeof(ip_payload));
  ASSERT_EQ(priority, 6);
}

TEST(Cfg80211, Classify8021d_Ipv6) {
  uint8_t ip_payload[] = {
      0x01,       0x01,      0x01, 0x01, 0x01, 0x01,  // dst addr
      0x02,       0x02,      0x02, 0x02, 0x02, 0x02,  // src addr
      0x86,       0xdd,                               // ipv6 ethertype
      0b11110101, 0b10000000                          // part of ipv6 header
  };
  uint8_t priority = brcmf_cfg80211_classify8021d(ip_payload, sizeof(ip_payload));
  ASSERT_EQ(priority, 3);
}

TEST(Cfg80211, Classify8021d_PayloadTooSmall) {
  uint8_t ip_payload[] = {
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // dst addr
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // src addr
      0x08, 0x00,                          // ipv4 ethertype
  };
  uint8_t priority = brcmf_cfg80211_classify8021d(ip_payload, sizeof(ip_payload));
  ASSERT_EQ(priority, 0);
}

TEST(Cfg80211, SetAssocConfWmmParam) {
  uint8_t ie[] = {
      0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,  // Supported rates
      // WMM param
      0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,  // WMM header
      0x80,                                            // Qos Info - U-ASPD enabled
      0x00,                                            // reserved
      0x03, 0xa4, 0x00, 0x00,                          // Best effort AC params
      0x27, 0xa4, 0x00, 0x00,                          // Background AC params
      0x42, 0x43, 0x5e, 0x00,                          // Video AC params
      0x62, 0x32, 0x2f, 0x00,                          // Voice AC params

      0xbb, 0xff,  // random trailing bytes
  };
  uint8_t expected_wmm_param[] = {0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00,
                                  0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00};
  brcmf_cfg80211_info cfg;
  cfg.conn_info.resp_ie = ie;
  cfg.conn_info.resp_ie_len = sizeof(ie);

  wlanif_assoc_confirm_t confirm;
  set_assoc_conf_wmm_param(&cfg, &confirm);
  EXPECT_TRUE(confirm.wmm_param_present);
  EXPECT_EQ(memcmp(confirm.wmm_param, expected_wmm_param, sizeof(expected_wmm_param)), 0);
  EXPECT_EQ(sizeof(expected_wmm_param), WLAN_WMM_PARAM_LEN);
}

TEST(Cfg80211, SetAssocConfWmmParam_WmmParamNotPresent) {
  uint8_t ie[] = {
      0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,  // Supported rates
  };
  brcmf_cfg80211_info cfg;
  cfg.conn_info.resp_ie = ie;
  cfg.conn_info.resp_ie_len = sizeof(ie);

  wlanif_assoc_confirm_t confirm;
  set_assoc_conf_wmm_param(&cfg, &confirm);
  EXPECT_FALSE(confirm.wmm_param_present);
}

TEST(Cfg80211, ChannelSwitchTest) {
  // The second and third arguments aren't required or used, so this is intended to test
  // a NULL first parameter.
  EXPECT_EQ(brcmf_notify_channel_switch(nullptr,  nullptr, nullptr), ZX_ERR_INVALID_ARGS);
}

}  // namespace
