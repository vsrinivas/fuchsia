// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <optional>
#include <tuple>

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/message.h>
#include <src/connectivity/wlan/drivers/wlanif/device.h>
#include <wlan/mlme/dispatcher.h>

namespace wlan_mlme = ::fuchsia::wlan::mlme;

bool multicast_promisc_enabled = false;

static zx_status_t hook_set_multicast_promisc(void* ctx, bool enable) {
  multicast_promisc_enabled = enable;
  return ZX_OK;
}

std::pair<zx::channel, zx::channel> make_channel() {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  return {std::move(local), std::move(remote)};
}

// Reads a fidl_msg_t from a channel.
struct FidlMessage {
  static std::optional<FidlMessage> read_from_channel(zx::channel* endpoint) {
    FidlMessage msg = {};
    auto status =
        endpoint->read(ZX_CHANNEL_READ_MAY_DISCARD, msg.bytes, msg.handles, sizeof(msg.bytes),
                       sizeof(msg.handles), &msg.actual_bytes, &msg.actual_handles);
    if (status != ZX_OK) {
      return {std::nullopt};
    }
    return {std::move(msg)};
  }

  fidl_msg_t get() {
    return {.bytes = bytes,
            .handles = handles,
            .num_bytes = actual_bytes,
            .num_handles = actual_handles};
  }

  fbl::Span<uint8_t> data() { return {bytes, actual_bytes}; }

  FIDL_ALIGNDECL uint8_t bytes[256];
  zx_handle_t handles[256];
  uint32_t actual_bytes;
  uint32_t actual_handles;
};

// Verify that receiving an ethernet SetParam for multicast promiscuous mode results in a call to
// wlanif_impl->set_muilticast_promisc.
TEST(MulticastPromiscMode, OnOff) {
  zx_status_t status;

  wlanif_impl_protocol_ops_t proto_ops = {.set_multicast_promisc = hook_set_multicast_promisc};
  wlanif_impl_protocol_t proto = {.ops = &proto_ops};
  wlanif::Device device(nullptr, proto);

  multicast_promisc_enabled = false;

  // Disable => Enable
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, true);

  // Enable => Enable
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, true);

  // Enable => Enable (any non-zero value should be treated as "true")
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0x80, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, true);

  // Enable => Disable
  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(multicast_promisc_enabled, false);
}

// Verify that we get a ZX_ERR_UNSUPPORTED back if the set_multicast_promisc hook is unimplemented.
TEST(MulticastPromiscMode, Unimplemented) {
  zx_status_t status;

  wlanif_impl_protocol_ops_t proto_ops = {};
  wlanif_impl_protocol_t proto = {.ops = &proto_ops};
  wlanif::Device device(nullptr, proto);

  multicast_promisc_enabled = false;

  status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(multicast_promisc_enabled, false);
}

TEST(Sme, ConnectSuccess) {
  wlanif_impl_protocol_ops_t proto_ops = {};
  wlanif_impl_protocol_t proto = {.ops = &proto_ops};
  wlanif::Device device(nullptr, proto);

  // Set-up test environment.
  auto [sme, mlme] = make_channel();
  auto [local, remote] = make_channel();
  auto status = fuchsia_wlan_mlme_ConnectorConnect(local.get(), mlme.release());
  ASSERT_EQ(status, ZX_OK);

  // Read the connect request from the channel.
  auto msg = FidlMessage::read_from_channel(&remote);
  ASSERT_TRUE(msg.has_value());

  // Hand the connect message to the device.
  auto fidl_msg = msg->get();
  status = device.Message(&fidl_msg, nullptr);
  ASSERT_EQ(status, ZX_OK);

  // Simulate an event from the vendor driver.
  auto eapol_conf_event =
      wlanif_eapol_confirm_t{.result_code = WLAN_EAPOL_RESULT_TRANSMISSION_FAILURE};
  device.EapolConf(&eapol_conf_event);

  // SME should have received a message from MLME. Read it.
  msg = FidlMessage::read_from_channel(&sme);
  ASSERT_TRUE(msg.has_value());

  // Verify received message is correct.
  auto hdr = wlan::FromBytes<fidl_message_header_t>(msg->data());
  ASSERT_NE(hdr, nullptr);
  auto ordinal = hdr->ordinal;
  ASSERT_TRUE(ordinal == fuchsia_wlan_mlme_MLMEEapolConfOrdinal ||
              ordinal == fuchsia_wlan_mlme_MLMEEapolConfGenOrdinal);

  auto eapol_conf = wlan::MlmeMsg<wlan_mlme::EapolConfirm>::Decode(msg->data(), ordinal);
  ASSERT_TRUE(eapol_conf.has_value());
  EXPECT_EQ(eapol_conf->body()->result_code, wlan_mlme::EapolResultCodes::TRANSMISSION_FAILURE);
}

TEST(Sme, ConnectAlreadyBound) {
  wlanif_impl_protocol_ops_t proto_ops = {};
  wlanif_impl_protocol_t proto = {.ops = &proto_ops};
  wlanif::Device device(nullptr, proto);

  // Perform initial connect request.
  auto [sme, mlme] = make_channel();
  auto status = device.Connect(std::move(mlme));
  ASSERT_EQ(status, ZX_OK);

  // Send another request.
  auto [new_sme, new_mlme] = make_channel();
  status = device.Connect(std::move(new_mlme));
  ASSERT_EQ(status, ZX_ERR_ALREADY_BOUND);
}
