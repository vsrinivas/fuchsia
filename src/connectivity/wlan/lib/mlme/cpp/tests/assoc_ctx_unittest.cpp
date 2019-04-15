// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fidl/cpp/vector.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/common/element.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/span.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/assoc_context.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rates_elements.h>

#include <optional>
#include <vector>

#include "test_utils.h"

namespace wlan {
namespace {

struct TestVector {
  std::vector<uint8_t> ap_basic_rate_set;
  std::vector<uint8_t> ap_op_rate_set;
  std::vector<SupportedRate> client_rates;
  std::optional<std::vector<SupportedRate>> want_rates;
};

const common::MacAddr TestMac({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});

namespace wlan_mlme = ::fuchsia::wlan::mlme;
using SR = SupportedRate;
constexpr auto SR_b = SupportedRate::basic;

// op_rate_set is a super set of basic_rate_set.
// Result is the intersection of ap_op_rate_set and client_rates.
// The basic-ness of client rates are disregarded and the basic-ness of AP is
// preserved.

void TestOnce(const TestVector& tv) {
  auto basic = ::fidl::VectorPtr(tv.ap_basic_rate_set);
  auto op = ::fidl::VectorPtr(tv.ap_op_rate_set);

  auto got_rates = BuildAssocReqSuppRates(basic, op, tv.client_rates);

  ASSERT_EQ(tv.want_rates.has_value(), got_rates.has_value());
  if (!got_rates.has_value()) {
    return;
  }

  EXPECT_EQ(tv.want_rates.value(), got_rates.value());
  for (size_t i = 0; i < got_rates->size(); ++i) {
    EXPECT_EQ(tv.want_rates.value()[i].val(), got_rates.value()[i].val());
  }
}

AssociationResponse* WriteAssocRespHdr(BufferWriter* w) {
  auto assoc_resp = w->Write<AssociationResponse>();
  assoc_resp->aid = 1234;
  assoc_resp->status_code = 2345;
  return assoc_resp;
}

Span<const uint8_t> WriteAssocRespElements(Span<uint8_t> buffer) {
  BufferWriter w(buffer);
  HtCapabilities ht_cap{};
  ht_cap.ht_cap_info.set_rx_stbc(1);
  ht_cap.ht_cap_info.set_tx_stbc(1);
  HtOperation ht_op{};
  VhtCapabilities vht_cap{};

  common::WriteHtCapabilities(&w, ht_cap);
  common::WriteHtOperation(&w, ht_op);
  common::WriteVhtCapabilities(&w, vht_cap);
  return w.WrittenData();
}

TEST(AssociationRatesTest, Success) {
  TestOnce({
      .ap_basic_rate_set = {1},
      .ap_op_rate_set = {1, 2},
      .client_rates = {SR{1}, SR{2}, SR{3}},
      .want_rates = {{SR_b(1), SR(2)}},
  });
}

TEST(AssociationRatesTest, SuccessWithDuplicateRates) {
  TestOnce({
      .ap_basic_rate_set = {1, 1},
      .ap_op_rate_set = {1},
      .client_rates = {SR{1}, SR{2}, SR{3}},
      .want_rates = {{SR_b(1)}},
  });
}

TEST(AssociationRatesTest, FailureNoApBasicRatesSupported) {
  TestOnce({
      .ap_basic_rate_set = {1},
      .ap_op_rate_set = {1},
      .client_rates = {SR{2}, SR{3}},
      .want_rates = {},
  });
}

TEST(AssociationRatesTest, FailureApBasicRatesPartiallySupported) {
  TestOnce({
      .ap_basic_rate_set = {1, 4},
      .ap_op_rate_set = {1, 4},
      .client_rates = {SR{1}, SR{2}, SR{3}},
      .want_rates = {},
  });
}

TEST(ParseAssocRespIe, ParseToFail) {
  const uint8_t expected[] = {
      // clang-format off
        // HT Capabilities IE
        45, 26,
        0xaa, 0xbb, 0x55, 0x0,  0x1,  0x2,  0x3,  0x4,
        0x5,  0x6,  0x7,  0x8,  0x9,  0xa,  0xb,  0xc,
        0xd,  0xe,  0xf,  0xdd, 0xee, 0x11, 0x22, 0x33,
        0x44, 0x77,
        // HT Operation IE
        61, 20, // (61, 20) is a corrupted value pair. Valid pair is (61, 22)
        36,  0x11, 0x22, 0x33, 0x44, 0x55, 0x0, 0x1,
        0x2, 0x3,  0x4,  0x5,  0x6,  0x7,  0x8, 0x9,
        0xa, 0xb,  0xc,  0xd,  0xe,  0xf,
      // clang-format on
  };

  auto ctx = ParseAssocRespIe(expected);
  EXPECT_FALSE(ctx.has_value());
}

TEST(ParseAssocRespIe, Parse) {
  size_t kBufLen = 512;
  std::vector<uint8_t> ie_chains(kBufLen);

  SupportedRate rates[3] = {SupportedRate{10}, SupportedRate{20},
                            SupportedRate{30}};
  HtCapabilities ht_cap{};
  HtOperation ht_op{};
  VhtCapabilities vht_cap{};
  VhtOperation vht_op{};

  ht_cap.ht_cap_info.set_rx_stbc(1);
  ht_cap.ht_cap_info.set_tx_stbc(0);
  ht_op.primary_chan = 199;
  ht_op.head.set_center_freq_seg2(123);
  vht_cap.vht_cap_info.set_num_sounding(5);
  vht_op.center_freq_seg0 = 42;

  BufferWriter elem_w(ie_chains);
  common::WriteHtCapabilities(&elem_w, ht_cap);
  common::WriteVhtOperation(&elem_w, vht_op);
  common::WriteHtOperation(&elem_w, ht_op);
  RatesWriter(rates).WriteSupportedRates(&elem_w);
  common::WriteVhtCapabilities(&elem_w, vht_cap);

  auto ctx = ParseAssocRespIe(elem_w.WrittenData());
  ASSERT_TRUE(ctx.has_value());
  EXPECT_EQ(rates[0], ctx->rates[0]);
  EXPECT_EQ(rates[1], ctx->rates[1]);
  EXPECT_EQ(rates[2], ctx->rates[2]);
  EXPECT_EQ(1, ctx->ht_cap->ht_cap_info.rx_stbc());
  EXPECT_EQ(0, ctx->ht_cap->ht_cap_info.tx_stbc());
  EXPECT_EQ(199, ctx->ht_op->primary_chan);
  EXPECT_EQ(123, ctx->ht_op->head.center_freq_seg2());
  EXPECT_EQ(5, ctx->vht_cap->vht_cap_info.num_sounding());
  EXPECT_EQ(42, ctx->vht_op->center_freq_seg0);
}

TEST(AssocContext, IntersectHtNoVht) {
  // Constructing client and BSS sample association context without VHT.
  auto bss_ctx = test_utils::FakeAssocCtx();
  bss_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);
  bss_ctx.ht_cap->ht_cap_info.set_rx_stbc(1);
  bss_ctx.ht_cap->ht_cap_info.set_tx_stbc(0);
  bss_ctx.vht_cap = {};
  bss_ctx.vht_op = {};

  auto client_ctx = test_utils::FakeAssocCtx();
  client_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);
  client_ctx.ht_cap->ht_cap_info.set_rx_stbc(1);
  client_ctx.ht_cap->ht_cap_info.set_tx_stbc(0);
  client_ctx.vht_cap = {};
  client_ctx.vht_op = {};

  auto ctx = IntersectAssocCtx(bss_ctx, client_ctx);
  // Verify VHT is not part of resulting context.
  EXPECT_EQ(std::nullopt, ctx.vht_cap);
  EXPECT_EQ(std::nullopt, ctx.vht_op);
  // Verify context's other fields contain expected value.
  EXPECT_TRUE(ctx.ht_cap.has_value());
  EXPECT_TRUE(ctx.ht_op.has_value());
  EXPECT_EQ(0, ctx.ht_cap->ht_cap_info.tx_stbc());
  EXPECT_EQ(0, ctx.ht_cap->ht_cap_info.rx_stbc());
  EXPECT_TRUE(ctx.is_cbw40_rx);
  EXPECT_FALSE(
      ctx.is_cbw40_tx);  // TODO(NET-1918): Revisit with rx/tx CBW40 capability
}

TEST(AssocContext, IntersectClientNoHT) {
  auto bss_ctx = test_utils::FakeAssocCtx();
  bss_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);

  auto client_ctx = test_utils::FakeAssocCtx();
  client_ctx.ht_cap = {};
  client_ctx.vht_cap = {};
  client_ctx.vht_op = {};

  auto ctx = IntersectAssocCtx(bss_ctx, client_ctx);
  EXPECT_FALSE(ctx.ht_cap.has_value());
  EXPECT_FALSE(ctx.vht_cap.has_value());
  EXPECT_FALSE(ctx.vht_op.has_value());
}

TEST(AssocContext, IntersectHtVht) {
  auto bss_ctx = test_utils::FakeAssocCtx();
  bss_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);

  auto client_ctx = test_utils::FakeAssocCtx();
  client_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);

  auto ctx = IntersectAssocCtx(bss_ctx, client_ctx);
  EXPECT_TRUE(ctx.vht_cap.has_value());
  EXPECT_TRUE(ctx.vht_op.has_value());
}

TEST(AssocContext, IntersectClientNoVht) {
  auto bss_ctx = test_utils::FakeAssocCtx();
  bss_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);

  auto client_ctx = test_utils::FakeAssocCtx();
  client_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);
  client_ctx.vht_cap = {};

  auto ctx = IntersectAssocCtx(bss_ctx, client_ctx);
  EXPECT_TRUE(ctx.ht_cap.has_value());
  EXPECT_TRUE(ctx.ht_op.has_value());
  EXPECT_FALSE(ctx.vht_cap.has_value());
  EXPECT_FALSE(ctx.vht_op.has_value());
}

TEST(AssocContext, IntersectBssNoHT) {
  auto bss_ctx = test_utils::FakeAssocCtx();
  bss_ctx.ht_cap = {};
  bss_ctx.ht_op = {};
  bss_ctx.vht_cap = {};
  bss_ctx.vht_op = {};

  auto client_ctx = test_utils::FakeAssocCtx();
  client_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);

  auto ctx = IntersectAssocCtx(bss_ctx, client_ctx);
  EXPECT_FALSE(ctx.ht_cap.has_value());
  EXPECT_FALSE(ctx.ht_op.has_value());
  EXPECT_FALSE(ctx.vht_cap.has_value());
  EXPECT_FALSE(ctx.vht_op.has_value());
}

TEST(AssocContext, IntersectBssNoVht) {
  auto bss_ctx = test_utils::FakeAssocCtx();
  bss_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);
  bss_ctx.vht_cap = {};
  bss_ctx.vht_op = {};

  auto client_ctx = test_utils::FakeAssocCtx();
  client_ctx.ht_cap->ht_cap_info.set_chan_width_set(
      HtCapabilityInfo::TWENTY_FORTY);

  auto ctx = IntersectAssocCtx(bss_ctx, client_ctx);
  EXPECT_TRUE(ctx.ht_cap.has_value());
  EXPECT_TRUE(ctx.ht_op.has_value());
  EXPECT_FALSE(ctx.vht_cap.has_value());
  EXPECT_FALSE(ctx.vht_op.has_value());
}

TEST(AssocContext, MakeBssAssocCtx) {
  uint8_t buffer[512]{};
  BufferWriter w(buffer);
  auto assoc_resp = WriteAssocRespHdr(&w);
  auto ie_chain = WriteAssocRespElements(w.RemainingBuffer());

  auto ctx = MakeBssAssocCtx(*assoc_resp, ie_chain, TestMac);
  ASSERT_TRUE(ctx.has_value());
  ASSERT_TRUE(ctx->ht_cap.has_value());
  EXPECT_TRUE(ctx->ht_op.has_value());
  EXPECT_TRUE(ctx->vht_cap.has_value());
  EXPECT_FALSE(ctx->vht_op.has_value());
  EXPECT_EQ(1, ctx->ht_cap->ht_cap_info.rx_stbc());
  EXPECT_EQ(1, ctx->ht_cap->ht_cap_info.tx_stbc());
}

TEST(AssocContext, ToDdk) {
  // TODO(NET-1959): Test more fields.

  auto ctx = test_utils::FakeAssocCtx();
  ctx.vht_cap = {};
  ctx.vht_op = {};

  auto ddk = ctx.ToDdk();
  EXPECT_TRUE(ddk.has_ht_cap);
  EXPECT_TRUE(ddk.has_ht_op);
  EXPECT_FALSE(ddk.has_vht_cap);
  EXPECT_FALSE(ddk.has_vht_op);
}

}  // namespace
}  // namespace wlan
