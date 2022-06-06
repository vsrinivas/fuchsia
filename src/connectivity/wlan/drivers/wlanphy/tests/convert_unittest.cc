// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../device.h"
#include "../driver.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace wlanphy {
namespace {

wlanphy_impl_protocol_ops_t make_ops_for_get_country(
    zx_status_t (*get_country)(void* ctx, wlanphy_country_t* out_country)) {
  return wlanphy_impl_protocol_ops_t{
      .get_supported_mac_roles =
          [](void* ctx,
             wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
             uint8_t* supported_mac_roles_count) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                         uint16_t* out_iface_id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_country = get_country,
      .set_ps_mode = [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_ps_mode = [](void* ctx, wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
  };
}

class WlanphyTest : public gtest::RealLoopFixture {
  // Enable timeouts only to test things locally, committed code should not use timeouts.
  static constexpr zx::duration kTimeout = zx::duration::infinite();

  bool RunLoopUntilOrFailure(fit::function<bool()> f) {
    return RunLoopWithTimeoutOrUntil([f = std::move(f)] { return HasFailure() || f(); }, kTimeout,
                                     zx::duration::infinite());
  }
};

TEST_F(WlanphyTest, GetCountryConvertsPrintableAndReturnsSuccess) {
  auto ops = make_ops_for_get_country([](void* ctx, wlanphy_country_t* out_country) {
    *out_country = {
        .alpha2 = {'U', 'S'},
    };
    return ZX_OK;
  });
  void* ctx = nullptr;
  wlanphy_init(&ctx);

  Device dev(nullptr, wlanphy_impl_protocol_t{.ops = &ops, .ctx = ctx});

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fidl::BindServer(dispatcher(), std::move(endpoints->server), &dev);
  fidl::WireClient client(std::move(endpoints->client), dispatcher());

  bool invoked_callback = false;
  client->GetCountry().Then(
      [&invoked_callback](fidl::WireUnownedResult<fuchsia_wlan_device::Phy::GetCountry>& result) {
        ASSERT_TRUE(result.ok()) << result.status_string();
        const auto& response = result.value();
        ASSERT_TRUE(response.is_ok()) << zx_status_get_string(response.error_value());
        const auto& value = *response.value();
        ASSERT_THAT(value.resp.alpha2, testing::ElementsAre('U', 'S'));
        invoked_callback = true;
      });

  RunLoopUntil([&invoked_callback]() { return invoked_callback; });
}

TEST_F(WlanphyTest, GetCountryConvertsNonPrintableAndReturnSuccess) {
  auto ops = make_ops_for_get_country([](void* ctx, wlanphy_country_t* out_country) {
    *out_country = {
        .alpha2 = {0x00, 0xff},
    };
    return ZX_OK;
  });
  void* ctx = nullptr;
  wlanphy_init(&ctx);

  Device dev(nullptr, wlanphy_impl_protocol_t{.ops = &ops, .ctx = ctx});

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fidl::BindServer(dispatcher(), std::move(endpoints->server), &dev);
  fidl::WireClient client(std::move(endpoints->client), dispatcher());

  bool invoked_callback = false;
  client->GetCountry().Then(
      [&invoked_callback](fidl::WireUnownedResult<fuchsia_wlan_device::Phy::GetCountry>& result) {
        ASSERT_TRUE(result.ok()) << result.status_string();
        const auto& response = result.value();
        ASSERT_TRUE(response.is_ok()) << zx_status_get_string(response.error_value());
        const auto& value = *response.value();
        ASSERT_THAT(value.resp.alpha2, testing::ElementsAre(0x00, 0xff));
        invoked_callback = true;
      });
  RunLoopUntil([&invoked_callback]() { return invoked_callback; });
}

wlanphy_impl_protocol_ops_t make_ops_for_get_ps_mode(
    zx_status_t (*get_ps_mode)(void* ctx, wlanphy_ps_mode_t* ps_mode)) {
  return wlanphy_impl_protocol_ops_t{
      .get_supported_mac_roles =
          [](void* ctx,
             wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
             uint8_t* supported_mac_roles_count) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                         uint16_t* out_iface_id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_country = [](void* ctx, wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .set_ps_mode = [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_ps_mode = get_ps_mode,
  };
}

TEST_F(WlanphyTest, GetPsModeReturnsSuccess) {
  auto ops = make_ops_for_get_ps_mode([](void* ctx, wlanphy_ps_mode_t* ps_mode) {
    ps_mode->ps_mode = POWER_SAVE_TYPE_PS_MODE_BALANCED;
    return ZX_OK;
  });
  void* ctx = nullptr;
  wlanphy_init(&ctx);

  Device dev(nullptr, wlanphy_impl_protocol_t{.ops = &ops, .ctx = ctx});

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fidl::BindServer(dispatcher(), std::move(endpoints->server), &dev);
  fidl::WireClient client(std::move(endpoints->client), dispatcher());

  bool invoked_callback = false;
  client->GetPsMode().Then(
      [&invoked_callback](fidl::WireUnownedResult<fuchsia_wlan_device::Phy::GetPsMode>& result) {
        ASSERT_TRUE(result.ok()) << result.status_string();
        const auto& response = result.value();
        ASSERT_TRUE(response.is_ok()) << zx_status_get_string(response.error_value());
        const auto& value = *response.value();
        EXPECT_EQ(value.resp, fuchsia_wlan_common::PowerSaveType::kPsModeBalanced);
        invoked_callback = true;
      });
  RunLoopUntil([&invoked_callback]() { return invoked_callback; });
}

}  // namespace
}  // namespace wlanphy
