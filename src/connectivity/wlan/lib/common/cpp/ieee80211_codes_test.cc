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

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>

#include <wlan/common/ieee80211_codes.h>

#include "gtest/gtest.h"

namespace wlan::common {

namespace {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

TEST(ConvertReasonCode, EnumMapsToUint16AsExpected) {
  ASSERT_EQ(static_cast<uint16_t>(1),
            ConvertReasonCode(wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON));
  ASSERT_EQ(static_cast<uint16_t>(66),
            ConvertReasonCode(wlan_ieee80211::ReasonCode::MESH_CHANNEL_SWITCH_UNSPECIFIED));
  ASSERT_EQ(static_cast<uint16_t>(128),
            ConvertReasonCode(wlan_ieee80211::ReasonCode::MLME_LINK_FAILED));
}

TEST(ConvertReasonCode, ReservedCodesMapAsExpected) {
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_0, ConvertReasonCode(static_cast<uint16_t>(0)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_67_TO_127,
            ConvertReasonCode(static_cast<uint16_t>(67)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_67_TO_127,
            ConvertReasonCode(static_cast<uint16_t>(68)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_67_TO_127,
            ConvertReasonCode(static_cast<uint16_t>(127)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_130_TO_65535,
            ConvertReasonCode(static_cast<uint16_t>(130)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_130_TO_65535,
            ConvertReasonCode(static_cast<uint16_t>(131)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::RESERVED_130_TO_65535,
            ConvertReasonCode(static_cast<uint16_t>(65535)));
}

TEST(ConvertReasonCode, DefinedCodesMapToEnumAsExpected) {
  ASSERT_EQ(wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON,
            ConvertReasonCode(static_cast<uint16_t>(1)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::MESH_CHANNEL_SWITCH_UNSPECIFIED,
            ConvertReasonCode(static_cast<uint16_t>(66)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::MLME_LINK_FAILED,
            ConvertReasonCode(static_cast<uint16_t>(128)));
  ASSERT_EQ(wlan_ieee80211::ReasonCode::FW_RX_STALLED,
            ConvertReasonCode(static_cast<uint16_t>(129)));
}

}  // namespace

}  // namespace wlan::common
