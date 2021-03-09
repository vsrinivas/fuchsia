/*
 * Copyright (c) 2021 The Fuchsia Authors
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

#include <memory>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/arp_logger.h"

namespace wlan::brcmfmac {

const uint32_t kDefaultIpAddr = 1;

// This test case verifies that the count value for a MAC address will be cleaned up when the log
// threshold is reached.
TEST(ArpLogger, ReachLogThreshold) {
  auto arp_logger = std::make_unique<ArpLogger>();
  uint16_t arp_count = 0;

  // Increase the count to kArpLogThreshold - 1.
  for (uint16_t k = 0; k < ArpLogger::kArpLogThreshold - 1; k++) {
    EXPECT_EQ(arp_logger->ArpRequestOut(kDefaultIpAddr), ZX_OK);
  }

  // Verify that the count indeed increased.
  EXPECT_EQ(arp_logger->GetArpCount(kDefaultIpAddr, &arp_count), ZX_OK);
  EXPECT_EQ(arp_count, ArpLogger::kArpLogThreshold - 1);

  // Increase the count to reach log threshold.
  EXPECT_EQ(arp_logger->ArpRequestOut(kDefaultIpAddr), ZX_OK);

  // Verify that the count is cleaned up.
  EXPECT_EQ(arp_logger->GetArpCount(kDefaultIpAddr, &arp_count), ZX_OK);
  EXPECT_EQ(arp_count, 0U);
}

// This test case verifies that the Arp Reply frame for a MAC address will clean up the unreplied
// frame count of it.
TEST(ArpLogger, ReplyResetCount) {
  auto arp_logger = std::make_unique<wlan::brcmfmac::ArpLogger>();
  uint16_t arp_count = 0;

  // Increase the count to kArpLogThreshold - 1.
  for (uint16_t k = 0; k < ArpLogger::kArpLogThreshold - 1; k++) {
    EXPECT_EQ(arp_logger->ArpRequestOut(kDefaultIpAddr), ZX_OK);
  }

  // Verify that the count indeed increased.
  EXPECT_EQ(arp_logger->GetArpCount(kDefaultIpAddr, &arp_count), ZX_OK);
  EXPECT_EQ(arp_count, ArpLogger::kArpLogThreshold - 1);

  // Clear the count with a Arp Reply frame.
  EXPECT_EQ(arp_logger->ArpReplyIn(kDefaultIpAddr), ZX_OK);

  // Verify that the count is cleaned up.
  EXPECT_EQ(arp_logger->GetArpCount(kDefaultIpAddr, &arp_count), ZX_OK);
  EXPECT_EQ(arp_count, 0U);
}

TEST(ArpLogger, ArpReqFrameSet) {
  auto arp_logger = std::make_unique<wlan::brcmfmac::ArpLogger>();
  const std::string fake_frame_str1("aaaa");
  const std::string fake_frame_str2("bbbb");

  EXPECT_EQ(arp_logger->AddArpRequestFrame(fake_frame_str1), true);
  EXPECT_EQ(arp_logger->GetArpReqFrameSize(), 1U);

  EXPECT_EQ(arp_logger->AddArpRequestFrame(fake_frame_str1), false);
  EXPECT_EQ(arp_logger->GetArpReqFrameSize(), 1U);

  EXPECT_EQ(arp_logger->AddArpRequestFrame(fake_frame_str2), true);
  EXPECT_EQ(arp_logger->GetArpReqFrameSize(), 2U);
}

}  // namespace wlan::brcmfmac
