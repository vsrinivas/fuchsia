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

#include <gmock/gmock.h>

#include "gtest/gtest.h"

namespace {

using ::testing::SizeIs;

TEST(Cfg80211, FindSsidInIes_Success) {
  const uint8_t ies[] = {
      0x00, 0x03, 0x66, 0x6f, 0x6f,                                // SSID
      0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,  // Supported rates
      0x07, 0x06, 0x55, 0x53, 0x20, 0x01, 0x0b, 0x1e,              // Country
  };

  auto ssid = brcmf_find_ssid_in_ies(ies, sizeof(ies));

  const uint8_t ssid_bytes[] = {0x66, 0x6f, 0x6f};
  ASSERT_EQ(ssid.size(), sizeof(ssid_bytes));
  EXPECT_EQ(std::memcmp(ssid.data(), ssid_bytes, sizeof(ssid_bytes)), 0);
}

TEST(Cfg80211, FindSsidInIes_Empty) {
  const uint8_t ies[] = {
      0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,  // Supported rates
      0x07, 0x06, 0x55, 0x53, 0x20, 0x01, 0x0b, 0x1e,              // Country
  };

  auto ssid = brcmf_find_ssid_in_ies(ies, sizeof(ies));
  ASSERT_THAT(ssid, SizeIs(0));
}

TEST(Cfg80211, ChannelSwitchTest) {
  // The second and third arguments aren't required or used, so this is intended to test
  // a NULL first parameter.
  EXPECT_EQ(brcmf_notify_channel_switch(nullptr, nullptr, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST(Cfg80211, ScanStatusReport) {
  std::string scan_status_report;

  // Check one status bit printed alone.
  EXPECT_EQ(ZX_ERR_UNAVAILABLE,
            brcmf_check_scan_status((1 << static_cast<size_t>(brcmf_scan_status_bit_t::ABORT)),
                                    &scan_status_report));

  EXPECT_EQ(scan_status_report, "ABORT (0x2)");

  // Check two status bits concatenated with '+'.
  EXPECT_EQ(
      ZX_ERR_UNAVAILABLE,
      brcmf_check_scan_status((1 << static_cast<size_t>(brcmf_scan_status_bit_t::BUSY)) |
                                  (1 << static_cast<size_t>(brcmf_scan_status_bit_t::SUPPRESS)),
                              &scan_status_report));

  EXPECT_EQ(scan_status_report, "BUSY+SUPPRESS (0x5)");

  // Check threes status bits concatenated with '+'.
  EXPECT_EQ(
      ZX_ERR_UNAVAILABLE,
      brcmf_check_scan_status((1 << static_cast<size_t>(brcmf_scan_status_bit_t::BUSY)) |
                                  (1 << static_cast<size_t>(brcmf_scan_status_bit_t::ABORT)) |
                                  (1 << static_cast<size_t>(brcmf_scan_status_bit_t::SUPPRESS)),
                              &scan_status_report));
  EXPECT_EQ(scan_status_report, "BUSY+ABORT+SUPPRESS (0x7)");
}

}  // namespace
