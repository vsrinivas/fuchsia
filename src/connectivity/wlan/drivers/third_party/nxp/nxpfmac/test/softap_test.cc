// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/softap.h"

#include <lib/sync/completion.h>
#include <netinet/ether.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_fullmac_ifc.h"

namespace {

using wlan::nxpfmac::SoftAp;
using wlan::nxpfmac::SoftApIfc;

constexpr uint8_t kTestChannel = 6;
constexpr uint32_t kTestBssIndex = 2;
constexpr uint8_t kTestSoftApSsid[] = "Test_SoftAP";

class TestSoftApIfc : public SoftApIfc {
  void OnStaConnectEvent(uint8_t* sta_mac_addr, uint8_t* ies, uint32_t ie_len) override {
    NXPF_INFO("STA connect event received");
  }
  void OnStaDisconnectEvent(uint8_t* sta_mac_addr, uint16_t reason_code) override {
    NXPF_INFO("Sta disconnect event received");
  }
};

struct SoftApTest : public zxtest::Test {
  void SetUp() override {
    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    context_ = wlan::nxpfmac::DeviceContext{.event_handler_ = &event_handler_,
                                            .ioctl_adapter_ = ioctl_adapter_.get()};
    context_.event_handler_ = &event_handler_;
    context_.ioctl_adapter_ = ioctl_adapter_.get();
  }

  wlan::nxpfmac::MlanMockAdapter mocks_;
  wlan::nxpfmac::MockBus mock_bus_;
  wlan::nxpfmac::EventHandler event_handler_;
  wlan::nxpfmac::DeviceContext context_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
  TestSoftApIfc test_ifc_;
};

TEST_F(SoftApTest, Constructible) {
  ASSERT_NO_FATAL_FAILURE(SoftAp(&test_ifc_, &context_, kTestBssIndex));
}

TEST_F(SoftApTest, Start) {
  constexpr uint32_t kBssIndex = 1;
  wlan_fullmac_start_req request = {.channel = kTestChannel};

  sync_completion_t ioctl_completion;

  mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->req_id == MLAN_IOCTL_BSS) {
      EXPECT_EQ(req->bss_index, kBssIndex);
      auto bss = reinterpret_cast<const mlan_ds_bss*>(req->pbuf);
      if (bss->sub_command == MLAN_OID_UAP_BSS_CONFIG) {
        if (req->action == MLAN_ACT_SET) {
          // BSS config set. Ensure SSID, channel and Band are correctly set.
          EXPECT_EQ(bss->param.bss_config.ssid.ssid_len, sizeof(kTestSoftApSsid));
          EXPECT_BYTES_EQ(bss->param.bss_config.ssid.ssid, kTestSoftApSsid,
                          bss->param.bss_config.ssid.ssid_len);
          EXPECT_EQ(bss->param.bss_config.channel, kTestChannel);
          EXPECT_EQ(bss->param.bss_config.bandcfg.chanBand, BAND_2GHZ);
          EXPECT_EQ(bss->param.bss_config.bandcfg.chanWidth, CHAN_BW_20MHZ);
        }
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      } else if (bss->sub_command == MLAN_OID_BSS_START) {
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        sync_completion_signal(&ioctl_completion);
        return MLAN_STATUS_PENDING;
      }
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  SoftAp softap(&test_ifc_, &context_, kBssIndex);

  memcpy(request.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  request.ssid.len = sizeof(kTestSoftApSsid);
  ASSERT_EQ(softap.Start(&request), WLAN_START_RESULT_SUCCESS);

  ASSERT_OK(sync_completion_wait(&ioctl_completion, ZX_TIME_INFINITE));
  // Starting it again should fail
  ASSERT_EQ(softap.Start(&request), WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED);
}

// Ensure appropriate data rates are set based on the requested band when starting the Soft AP.
TEST_F(SoftApTest, CheckRates) {
  constexpr uint32_t kBssIndex = 1;
  constexpr uint8_t kTest24GChannel = 9;
  constexpr uint8_t kTest5GChannel = 36;
  constexpr uint8_t band_a_rates[MLAN_SUPPORTED_RATES] = {12, 18, 24, 36, 176, 72, 96, 108};
  constexpr uint8_t band_bg_rates[MLAN_SUPPORTED_RATES] = {130, 132, 139, 150, 12, 18,
                                                           24,  36,  48,  72,  96, 108};

  sync_completion_t ioctl_completion;

  mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->req_id == MLAN_IOCTL_BSS) {
      EXPECT_EQ(req->bss_index, kBssIndex);
      auto bss = reinterpret_cast<const mlan_ds_bss*>(req->pbuf);
      if (bss->sub_command == MLAN_OID_UAP_BSS_CONFIG) {
        if (req->action == MLAN_ACT_SET) {
          // BSS config set. Ensure SSID, channel and Band are correctly set.
          EXPECT_EQ(bss->param.bss_config.ssid.ssid_len, sizeof(kTestSoftApSsid));
          EXPECT_BYTES_EQ(bss->param.bss_config.ssid.ssid, kTestSoftApSsid,
                          bss->param.bss_config.ssid.ssid_len);
          EXPECT_EQ(bss->param.bss_config.bandcfg.chanWidth, CHAN_BW_20MHZ);
          if (bss->param.bss_config.bandcfg.chanBand == BAND_2GHZ) {
            EXPECT_BYTES_EQ(bss->param.bss_config.rates, band_bg_rates, MAX_DATA_RATES);
          } else if (bss->param.bss_config.bandcfg.chanBand == BAND_5GHZ) {
            EXPECT_BYTES_EQ(bss->param.bss_config.rates, band_a_rates, MAX_DATA_RATES);
          }
        }
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      } else if (bss->sub_command == MLAN_OID_BSS_START) {
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        sync_completion_signal(&ioctl_completion);
        return MLAN_STATUS_PENDING;
      }
    } else if ((req->req_id == MLAN_IOCTL_RATE) && (req->action == MLAN_ACT_GET)) {
      auto rate_cfg = reinterpret_cast<mlan_ds_rate*>(req->pbuf);
      EXPECT_EQ(rate_cfg->param.rate_band_cfg.bss_mode, MLAN_BSS_MODE_INFRA);
      if (rate_cfg->param.rate_band_cfg.config_bands & BAND_A) {
        memcpy(rate_cfg->param.rates, band_a_rates, sizeof(band_a_rates));
      } else if (rate_cfg->param.rate_band_cfg.config_bands & (BAND_B | BAND_G)) {
        memcpy(rate_cfg->param.rates, band_bg_rates, sizeof(band_bg_rates));
      }
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
      return MLAN_STATUS_PENDING;
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  SoftAp softap(&test_ifc_, &context_, kBssIndex);

  // Start the Soft AP on a 2.4Ghz channel
  wlan_fullmac_start_req start_req = {.channel = kTest24GChannel};
  memcpy(start_req.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  start_req.ssid.len = sizeof(kTestSoftApSsid);
  ASSERT_EQ(softap.Start(&start_req), WLAN_START_RESULT_SUCCESS);
  ASSERT_OK(sync_completion_wait(&ioctl_completion, ZX_TIME_INFINITE));

  // Stop the Soft AP.
  wlan_fullmac_stop_req stop_req;
  memcpy(stop_req.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  stop_req.ssid.len = sizeof(kTestSoftApSsid);
  ASSERT_EQ(softap.Stop(&stop_req), WLAN_STOP_RESULT_SUCCESS);

  // Start the Soft AP on a 5Ghz channel
  start_req.channel = kTest5GChannel;
  ASSERT_EQ(softap.Start(&start_req), WLAN_START_RESULT_SUCCESS);
}

TEST_F(SoftApTest, Stop) {
  constexpr uint32_t kBssIndex = 1;
  wlan_fullmac_start_req start_req = {.channel = kTestChannel};
  sync_completion_t ioctl_completion;

  // Test that stopping a SoftAP works as expected.
  mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss*>(req->pbuf);
      if (bss->sub_command == MLAN_OID_UAP_BSS_RESET) {
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        sync_completion_signal(&ioctl_completion);
        return MLAN_STATUS_PENDING;
      }
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  SoftAp softap(&test_ifc_, &context_, kBssIndex);
  wlan_fullmac_stop_req stop_req;

  memcpy(stop_req.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  stop_req.ssid.len = sizeof(kTestSoftApSsid);

  // Attempt to stop the Soft AP before it is started should fail
  ASSERT_EQ(WLAN_STOP_RESULT_BSS_ALREADY_STOPPED, softap.Stop(&stop_req));

  // Start the Soft AP.
  memcpy(start_req.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  start_req.ssid.len = sizeof(kTestSoftApSsid);

  ASSERT_EQ(softap.Start(&start_req), WLAN_START_RESULT_SUCCESS);

  uint8_t wrong_ssid[] = "Wrong_SoftAP";
  // Attempt to stop a different Soft AP (wrong ssid)
  memcpy(stop_req.ssid.data, wrong_ssid, sizeof(wrong_ssid));
  stop_req.ssid.len = sizeof(wrong_ssid);
  ASSERT_EQ(softap.Stop(&stop_req), WLAN_STOP_RESULT_INTERNAL_ERROR);

  // Stopping the correct Soft AP should succeed.
  memcpy(stop_req.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  stop_req.ssid.len = sizeof(kTestSoftApSsid);
  ASSERT_EQ(softap.Stop(&stop_req), WLAN_STOP_RESULT_SUCCESS);

  sync_completion_wait(&ioctl_completion, ZX_TIME_INFINITE);

  // Now that we're successfully stopped make sure calling stop again fails.
  ASSERT_EQ(WLAN_STOP_RESULT_BSS_ALREADY_STOPPED, softap.Stop(&stop_req));
  // And Start can be called again
  ASSERT_EQ(softap.Start(&start_req), WLAN_START_RESULT_SUCCESS);
}

TEST_F(SoftApTest, StaConnectDisconnectSoftAp) {
  constexpr uint8_t kTestSoftApClient[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5};
  constexpr uint32_t kBssIndex = 1;
  wlan_fullmac_start_req request = {.channel = kTestChannel};

  sync_completion_t ioctl_completion;
  class TestSoftApIfc : public SoftApIfc {
    void OnStaConnectEvent(uint8_t* sta_mac_addr, uint8_t* ies, uint32_t ie_len) override {
      memcpy(connect_sta, sta_mac_addr, ETH_ALEN);
    }
    void OnStaDisconnectEvent(uint8_t* sta_mac_addr, uint16_t reason_code) override {
      memcpy(disconnect_sta, sta_mac_addr, ETH_ALEN);
    }

   public:
    uint8_t connect_sta[ETH_ALEN] = {};
    uint8_t disconnect_sta[ETH_ALEN] = {};
  };
  TestSoftApIfc test_ifc;

  mocks_.SetOnMlanIoctl([&](t_void*, pmlan_ioctl_req req) -> mlan_status {
    if (req->req_id == MLAN_IOCTL_BSS) {
      EXPECT_EQ(req->bss_index, kBssIndex);
      auto bss = reinterpret_cast<const mlan_ds_bss*>(req->pbuf);
      if (bss->sub_command == MLAN_OID_UAP_BSS_CONFIG) {
        if (req->action == MLAN_ACT_SET) {
          // BSS config set. Ensure SSID, channel and Band are correctly set.
          EXPECT_EQ(bss->param.bss_config.ssid.ssid_len, sizeof(kTestSoftApSsid));
          EXPECT_BYTES_EQ(bss->param.bss_config.ssid.ssid, kTestSoftApSsid,
                          bss->param.bss_config.ssid.ssid_len);
          EXPECT_EQ(bss->param.bss_config.channel, kTestChannel);
          EXPECT_EQ(bss->param.bss_config.bandcfg.chanBand, BAND_2GHZ);
          EXPECT_EQ(bss->param.bss_config.bandcfg.chanWidth, CHAN_BW_20MHZ);
        }
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      } else if (bss->sub_command == MLAN_OID_BSS_START) {
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        sync_completion_signal(&ioctl_completion);
        return MLAN_STATUS_PENDING;
      }
    }
    // Return success for everything else.
    return MLAN_STATUS_SUCCESS;
  });

  SoftAp softap(&test_ifc, &context_, kBssIndex);

  memcpy(request.ssid.data, kTestSoftApSsid, sizeof(kTestSoftApSsid));
  request.ssid.len = sizeof(kTestSoftApSsid);
  ASSERT_EQ(softap.Start(&request), WLAN_START_RESULT_SUCCESS);

  ASSERT_OK(sync_completion_wait(&ioctl_completion, ZX_TIME_INFINITE));

  // STA connect event handling.
  uint8_t event_buf[sizeof(mlan_event) + ETH_ALEN + 2];
  auto event = reinterpret_cast<pmlan_event>(event_buf);
  event->event_id = MLAN_EVENT_ID_UAP_FW_STA_CONNECT;
  memcpy(event->event_buf, kTestSoftApClient, sizeof(kTestSoftApClient));
  event->bss_index = kBssIndex;
  // Set the event len to an invalid value, event should not be indicated to the ifc.
  event->event_len = 4;
  event_handler_.OnEvent(event);
  EXPECT_BYTES_NE(kTestSoftApClient, test_ifc.connect_sta, ETH_ALEN);
  // Now send the correct event length.
  event->event_len = sizeof(kTestSoftApClient);
  event_handler_.OnEvent(event);
  EXPECT_BYTES_EQ(kTestSoftApClient, test_ifc.connect_sta, ETH_ALEN);

  // STA disconnect event handling.
  event->event_id = MLAN_EVENT_ID_UAP_FW_STA_DISCONNECT;
  memcpy(event->event_buf + 2, kTestSoftApClient, sizeof(kTestSoftApClient));
  event->bss_index = kBssIndex;
  // Set the event length to an invalid value, event should not be indicated to the ifc.
  event->event_len = sizeof(kTestSoftApClient);
  event_handler_.OnEvent(event);
  EXPECT_BYTES_NE(kTestSoftApClient, test_ifc.disconnect_sta, ETH_ALEN);
  // Now set the correct event length.
  event->event_len = sizeof(kTestSoftApClient) + 2;
  event_handler_.OnEvent(event);
  EXPECT_BYTES_EQ(kTestSoftApClient, test_ifc.disconnect_sta, ETH_ALEN);
}

}  // namespace
