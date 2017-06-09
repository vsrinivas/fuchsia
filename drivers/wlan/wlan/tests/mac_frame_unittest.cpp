// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_frame.h"
#include "wlan.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace wlan {
namespace {

TEST(ProbeRequest, Validate) {
    uint8_t buf[128];
    ElementWriter writer(buf, sizeof(buf));

    ASSERT_TRUE(writer.write<SsidElement>("test ssid"));

    std::vector<uint8_t> rates{2, 4, 11, 22};
    ASSERT_TRUE(writer.write<SupportedRatesElement>(rates));

    auto probe_request = FromBytes<ProbeRequest>(buf, writer.size());
    EXPECT_TRUE(probe_request->Validate(writer.size()));
}

TEST(ProbeRequest, OutOfOrderElements) {
    uint8_t buf[128];
    ElementWriter writer(buf, sizeof(buf));

    std::vector<uint8_t> rates{2, 4, 11, 22};
    ASSERT_TRUE(writer.write<SupportedRatesElement>(rates));

    ASSERT_TRUE(writer.write<SsidElement>("test ssid"));

    auto probe_request = FromBytes<ProbeRequest>(buf, writer.size());
    EXPECT_FALSE(probe_request->Validate(writer.size()));
}

TEST(ProbeRequest, InvalidElement) {
    uint8_t buf[128];
    ElementWriter writer(buf, sizeof(buf));

    ASSERT_TRUE(writer.write<SsidElement>("test ssid"));
    ASSERT_TRUE(writer.write<CfParamSetElement>(1, 2, 3, 4));

    auto probe_request = FromBytes<ProbeRequest>(buf, writer.size());
    EXPECT_FALSE(probe_request->Validate(writer.size()));
}

}  // namespace
}  // namespace wlan
