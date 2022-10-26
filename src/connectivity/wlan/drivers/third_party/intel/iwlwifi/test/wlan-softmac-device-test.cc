// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlan-softmac-device.h"

#include <fidl/fuchsia.wlan.ieee80211/cpp/wire_types.h>
#include <lib/mock-function/mock-function.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <iterator>
#include <list>
#include <memory>
#include <utility>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/common.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/softmac.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-nvm-parse.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/scoped_utils.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/stats.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/mock-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/wlan-pkt-builder.h"

namespace wlan::testing {
namespace {

constexpr size_t kListenInterval = 100;
constexpr uint8_t kInvalidBandIdFillByte = 0xa5;
constexpr wlan_band_t kInvalidBandId = 0xa5;
constexpr zx_handle_t kDummyMlmeChannel = 73939133;  // An arbitrary value not ZX_HANDLE_INVALID

class WlanSoftmacDeviceTest : public SingleApTest,
                              public fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmacIfc> {
 public:
  WlanSoftmacDeviceTest() : test_arena_(nullptr) {
    mvmvif_ = reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));
    mvmvif_->mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());
    mvmvif_->mlme_channel = kDummyMlmeChannel;
    mvmvif_->mac_role = WLAN_MAC_ROLE_CLIENT;
    mvmvif_->bss_conf = {.beacon_int = kListenInterval};

    device_ = new ::wlan::iwlwifi::WlanSoftmacDevice(sim_trans_.fake_parent(),
                                                     sim_trans_.iwl_trans(), 0, mvmvif_);

    device_->DdkAdd("sim-iwlwifi-wlansoftmac", DEVICE_ADD_NON_BINDABLE);
    device_->DdkAsyncRemove();

    auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmac>();
    ASSERT_FALSE(endpoints.is_error());

    // Create a dispatcher for the client end of Wlansoftmac protocol to wait on the runtime
    // channel.
    auto dispatcher = fdf::Dispatcher::Create(
        0, "wlansoftmac_client_test", [&](fdf_dispatcher_t*) { client_completion_.Signal(); });
    ASSERT_FALSE(dispatcher.is_error());
    client_dispatcher_ = *std::move(dispatcher);

    // Create a dispatcher for the server end of WlansoftmacIfc protocol to wait on the runtime
    // channel.
    dispatcher = fdf::Dispatcher::Create(0, "wlansoftmacifc_server_test",
                                         [&](fdf_dispatcher_t*) { server_completion_.Signal(); });
    ASSERT_FALSE(dispatcher.is_error());
    server_dispatcher_ = *std::move(dispatcher);

    client_ = fdf::WireSharedClient<fuchsia_wlan_softmac::WlanSoftmac>(std::move(endpoints->client),
                                                                       client_dispatcher_.get());

    // TODO(fxbug.dev/106669): Increase test fidelity. Call mac_init() here as what DdkInit() in
    // real case does, instead of manually initialize mvmvif above to meet the minimal requirements
    // of the test cases. Note: device_->zxdev()->InitOp() will invoke DdkInit().

    // Create the server dispatcher of WlanSoftmac protocol for wlan-softmac-device.
    device_->InitServerDispatcher();

    // Create the client dispatcher of WlanSoftmacIfc protocol for wlan-softmac-device.
    device_->InitClientDispatcher();

    device_->DdkServiceConnect(fidl::DiscoverableProtocolName<fuchsia_wlan_softmac::WlanSoftmac>,
                               endpoints->server.TakeHandle());

    // Create test arena.
    auto arena = fdf::Arena::Create(0, 0);
    ASSERT_FALSE(arena.is_error());

    test_arena_ = *std::move(arena);
  }

  ~WlanSoftmacDeviceTest() {
    if (!release_called_) {
      mock_ddk::ReleaseFlaggedDevices(device_->zxdev());
    }
    client_dispatcher_.ShutdownAsync();
    server_dispatcher_.ShutdownAsync();

    client_completion_.Wait();
    server_completion_.Wait();
  }

  void Status(StatusRequestView request, fdf::Arena& arena,
              StatusCompleter::Sync& completer) override {
    // Overriding the virtual function, not being used at this point.
    completer.buffer(arena).Reply();
  }
  void Recv(RecvRequestView request, fdf::Arena& arena, RecvCompleter::Sync& completer) override {
    recv_called_ = true;
    completer.buffer(arena).Reply();
  }
  void CompleteTx(CompleteTxRequestView request, fdf::Arena& arena,
                  CompleteTxCompleter::Sync& completer) override {
    // Overriding the virtual function, not being used at this point.
    completer.buffer(arena).Reply();
  }
  void ReportTxStatus(ReportTxStatusRequestView request, fdf::Arena& arena,
                      ReportTxStatusCompleter::Sync& completer) override {
    // Overriding the virtual function, not being used at this point.
    completer.buffer(arena).Reply();
  }
  void ScanComplete(ScanCompleteRequestView request, fdf::Arena& arena,
                    ScanCompleteCompleter::Sync& completer) override {
    // Overriding the virtual function, not being used at this point.
    completer.buffer(arena).Reply();
  }

 protected:
  struct iwl_mvm_vif* mvmvif_;
  ::wlan::iwlwifi::WlanSoftmacDevice* device_;

  fdf::WireSharedClient<fuchsia_wlan_softmac::WlanSoftmac> client_;
  fdf::Dispatcher client_dispatcher_;
  fdf::Dispatcher server_dispatcher_;
  fdf::Arena test_arena_;
  libsync::Completion client_completion_;
  libsync::Completion server_completion_;
  // mock_ddk::ReleaseFlaggedDevices() cannot be called twice, but it's required to be called in
  // come test cases.
  bool release_called_ = false;
  // The marks of WlanSoftmacIfc function calls.
  bool recv_called_ = false;
};

//////////////////////////////////// Helper Functions  /////////////////////////////////////////////

TEST_F(WlanSoftmacDeviceTest, ComposeBandList) {
  struct iwl_nvm_data nvm_data;
  fuchsia_wlan_common::WlanBand bands[fuchsia_wlan_common::wire::kMaxBands];

  // nothing enabled
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  EXPECT_EQ(0, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(kInvalidBandId, uint8_t{bands[0]});
  EXPECT_EQ(kInvalidBandId, uint8_t{bands[1]});

  // 2.4GHz only
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_24ghz_enable = true;
  EXPECT_EQ(1, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kTwoGhz, bands[0]);
  EXPECT_EQ(kInvalidBandId, uint8_t{bands[1]});

  // 5GHz only
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_52ghz_enable = true;
  EXPECT_EQ(1, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kFiveGhz, bands[0]);
  EXPECT_EQ(kInvalidBandId, uint8_t{bands[1]});

  // both bands enabled
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_24ghz_enable = true;
  nvm_data.sku_cap_band_52ghz_enable = true;
  EXPECT_EQ(2, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kTwoGhz, bands[0]);
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kFiveGhz, bands[1]);
}

TEST_F(WlanSoftmacDeviceTest, FillBandCapabilityList) {
  // The default 'nvm_data' is loaded from test/sim-default-nvm.cc.

  const struct iwl_nvm_data* nvm_data = iwl_trans_get_mvm(sim_trans_.iwl_trans())->nvm_data;
  fuchsia_wlan_common::WlanBand bands[fuchsia_wlan_common::wire::kMaxBands];
  size_t band_cap_count = compose_band_list(nvm_data, bands);
  ASSERT_LE(band_cap_count, fuchsia_wlan_common::wire::kMaxBands);

  fuchsia_wlan_softmac::wire::WlanSoftmacBandCapability
      band_cap_list[fuchsia_wlan_common_MAX_BANDS] = {};
  fill_band_cap_list(nvm_data, bands, band_cap_count, band_cap_list);

  // 2.4Ghz
  fuchsia_wlan_softmac::wire::WlanSoftmacBandCapability* band_cap = &band_cap_list[0];
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kTwoGhz, band_cap->band);
  EXPECT_TRUE(band_cap->ht_supported);
  EXPECT_EQ(12, band_cap->basic_rate_count);
  EXPECT_EQ(2, band_cap->basic_rate_list[0]);     // 1Mbps
  EXPECT_EQ(108, band_cap->basic_rate_list[11]);  // 54Mbps
  EXPECT_EQ(13, band_cap->operating_channel_count);
  EXPECT_EQ(1, band_cap->operating_channel_list[0]);
  EXPECT_EQ(13, band_cap->operating_channel_list[12]);
  // 5GHz
  band_cap = &band_cap_list[1];
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kFiveGhz, band_cap->band);
  EXPECT_TRUE(band_cap->ht_supported);
  EXPECT_EQ(8, band_cap->basic_rate_count);
  EXPECT_EQ(12, band_cap->basic_rate_list[0]);   // 6Mbps
  EXPECT_EQ(108, band_cap->basic_rate_list[7]);  // 54Mbps
  EXPECT_EQ(25, band_cap->operating_channel_count);
  EXPECT_EQ(36, band_cap->operating_channel_list[0]);
  EXPECT_EQ(165, band_cap->operating_channel_list[24]);
}

TEST_F(WlanSoftmacDeviceTest, FillBandCapabilityListOnly5GHz) {
  // The default 'nvm_data' is loaded from test/sim-default-nvm.cc.

  fuchsia_wlan_common::WlanBand bands[fuchsia_wlan_common::wire::kMaxBands] = {
      fuchsia_wlan_common::WlanBand::kFiveGhz,
  };
  fuchsia_wlan_softmac::wire::WlanSoftmacBandCapability
      band_cap_list[fuchsia_wlan_common::wire::kMaxBands] = {};

  fill_band_cap_list(iwl_trans_get_mvm(sim_trans_.iwl_trans())->nvm_data, bands, 1, band_cap_list);
  // 5GHz
  fuchsia_wlan_softmac::wire::WlanSoftmacBandCapability* band_cap = &band_cap_list[0];
  EXPECT_EQ(fuchsia_wlan_common::WlanBand::kFiveGhz, band_cap->band);
  EXPECT_TRUE(band_cap->ht_supported);
  EXPECT_EQ(8, band_cap->basic_rate_count);
  EXPECT_EQ(12, band_cap->basic_rate_list[0]);   // 6Mbps
  EXPECT_EQ(108, band_cap->basic_rate_list[7]);  // 54Mbps
  EXPECT_EQ(25, band_cap->operating_channel_count);
  EXPECT_EQ(36, band_cap->operating_channel_list[0]);
  EXPECT_EQ(165, band_cap->operating_channel_list[24]);
  // index 1 should be empty.
  band_cap = &band_cap_list[1];
  EXPECT_FALSE(band_cap->ht_supported);
  EXPECT_EQ(0, band_cap->basic_rate_count);
  EXPECT_EQ(0, band_cap->operating_channel_count);
}

TEST_F(WlanSoftmacDeviceTest, Query) {
  auto result = client_.sync().buffer(test_arena_)->Query();
  auto& info = result->value()->info;
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_TRUE(info.has_mac_role());
  EXPECT_EQ(fuchsia_wlan_common::wire::WlanMacRole::kClient, info.mac_role());

  //
  // The below code assumes the test/sim-default-nvm.cc contains 2 bands.
  //
  //   .band_cap_list[0]: fuchsia_wlan_common::WlanBand::kTwoGhz
  //   .band_cap_list[1]: fuchsia_wlan_common::WlanBand::kFiveGhz
  //
  ASSERT_EQ(2, info.band_caps().count());
  EXPECT_EQ(12, info.band_caps().data()[0].basic_rate_count);
  EXPECT_EQ(2, info.band_caps().data()[0].basic_rate_list[0]);     // 1 Mbps
  EXPECT_EQ(36, info.band_caps().data()[0].basic_rate_list[7]);    // 18 Mbps
  EXPECT_EQ(108, info.band_caps().data()[0].basic_rate_list[11]);  // 54 Mbps
  EXPECT_EQ(8, info.band_caps().data()[1].basic_rate_count);
  EXPECT_EQ(12, info.band_caps().data()[1].basic_rate_list[0]);  // 6 Mbps
  EXPECT_EQ(165, info.band_caps().data()[1].operating_channel_list[24]);
}

TEST_F(WlanSoftmacDeviceTest, DiscoveryFeatureQuery) {
  auto result = client_.sync().buffer(test_arena_)->QueryDiscoverySupport();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_TRUE(result->value()->resp.scan_offload.supported);
  EXPECT_FALSE(result->value()->resp.probe_response_offload.supported);
}

TEST_F(WlanSoftmacDeviceTest, MacSublayerFeatureQuery) {
  auto result = client_.sync().buffer(test_arena_)->QueryMacSublayerSupport();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_FALSE(result->value()->resp.rate_selection_offload.supported);
  EXPECT_EQ(result->value()->resp.device.mac_implementation_type,
            fuchsia_wlan_common::wire::MacImplementationType::kSoftmac);
  EXPECT_FALSE(result->value()->resp.device.is_synthetic);
  EXPECT_EQ(result->value()->resp.data_plane.data_plane_type,
            fuchsia_wlan_common::wire::DataPlaneType::kEthernetDevice);
}

TEST_F(WlanSoftmacDeviceTest, SecurityFeatureQuery) {
  auto result = client_.sync().buffer(test_arena_)->QuerySecuritySupport();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_TRUE(result->value()->resp.mfp.supported);
  EXPECT_FALSE(result->value()->resp.sae.driver_handler_supported);
  EXPECT_TRUE(result->value()->resp.sae.sme_handler_supported);
}

TEST_F(WlanSoftmacDeviceTest, SpectrumManagementFeatureQuery) {
  auto result = client_.sync().buffer(test_arena_)->QuerySpectrumManagementSupport();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_FALSE(result->value()->resp.dfs.supported);
}

TEST_F(WlanSoftmacDeviceTest, MacStart) {
  // Created the end points for WlanSoftmacIfc protocol, and pass the client end to
  // WlanSoftmacDevice.
  auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmacIfc>();
  ASSERT_FALSE(endpoints.is_error());

  fdf::BindServer<fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmacIfc>>(
      server_dispatcher_.get(), std::move(endpoints->server), this);

  // This FIDL call should invoke mac_start() and pass the pointer of WlanSoftmacDeviceTest to it,
  // the pointer will be copied to mvmvif_->ifc.ctx.
  auto result = client_.sync().buffer(test_arena_)->Start(std::move(endpoints->client));
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result->is_error());
}

TEST_F(WlanSoftmacDeviceTest, MacStartOnlyOneMlmeChannelAllowed) {
  // The normal case. A channel will be transferred to MLME.
  constexpr zx_handle_t kMlmeChannel = static_cast<zx_handle_t>(0xF001);

  // Manually set mlme channel here, in reality, this is set when WlanphyImplDevice::CreateIface()
  // is called.
  mvmvif_->mlme_channel = kMlmeChannel;

  {
    // Don't need to bind the server here because we won't need to use WlanSoftmacIfc calls.
    auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmacIfc>();

    ASSERT_FALSE(endpoints.is_error());

    auto result = client_.sync().buffer(test_arena_)->Start(std::move(endpoints->client));
    EXPECT_TRUE(result.ok());
    EXPECT_FALSE(result->is_error());
    // Verify the channel returned.
    ASSERT_EQ(result->value()->sme_channel, kMlmeChannel);
    ASSERT_EQ(mvmvif_->mlme_channel, ZX_HANDLE_INVALID);  // Driver no longer holds the ownership.
  }

  {
    // Create another pair of endpoints for the next call.
    auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmacIfc>();

    // Since the driver no longer owns the handle, the start should fail.
    auto result = client_.sync().buffer(test_arena_)->Start(std::move(endpoints->client));
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result->is_error());
    EXPECT_EQ(ZX_ERR_ALREADY_BOUND, result->error_value());
  }
}

TEST_F(WlanSoftmacDeviceTest, SingleRxPacket) {
  // Created the end points for WlanSoftmacIfc protocol, and pass the client end to
  // WlanSoftmacDevice.
  auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmacIfc>();
  ASSERT_FALSE(endpoints.is_error());

  fdf::BindServer<fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmacIfc>>(
      server_dispatcher_.get(), std::move(endpoints->server), this);

  // This FIDL call should invoke mac_start() and pass the pointer of WlanSoftmacDeviceTest to it,
  // the pointer will be copied to mvmvif_->ifc.ctx.
  auto result = client_.sync().buffer(test_arena_)->Start(std::move(endpoints->client));

  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result->is_error());

  // Create an dummy rx packet.
  // TODO(fxbug.dev/99777): Integrate it into WlanPktBuilder.
  const uint8_t kMacPkt[] = {
      0x08, 0x01,                          // frame_ctrl
      0x00, 0x00,                          // duration
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // MAC1
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC2
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC3
      0x00, 0x00,                          // seq_ctrl
      0x45, 0x00, 0x55, 0x66, 0x01, 0x83,  // random IP packet...
  };

  wlan_rx_packet_t rx_packet = {
      .mac_frame_buffer = &kMacPkt[0],
      .mac_frame_size = sizeof(kMacPkt),
      .info =
          {
              .rx_flags = 0,
              .phy = WLAN_PHY_TYPE_DSSS,
          },
  };

  // The above lines should enable WlanSoftmacIfc FIDL calls, and the lines below is verifing that.
  mvmvif_->ifc.recv(mvmvif_->ifc.ctx, &rx_packet);
  EXPECT_TRUE(recv_called_);
}

TEST_F(WlanSoftmacDeviceTest, Release) {
  // Create a channel. Let this test case holds one end while driver holds the other end.
  char dummy[1];
  zx_handle_t case_end;
  ASSERT_EQ(zx_channel_create(0 /* option */, &case_end, &mvmvif_->mlme_channel), ZX_OK);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0), ZX_OK);

  // Call release and the sme channel should be closed so that we will get a peer-close error while
  // trying to write any data to it.
  mock_ddk::ReleaseFlaggedDevices(device_->zxdev());
  release_called_ = true;
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0),
            ZX_ERR_PEER_CLOSED);
}

// The class for WLAN device MAC testing.
//
class MacInterfaceTest : public WlanSoftmacDeviceTest, public MockTrans {
 public:
  MacInterfaceTest() {
    zx_handle_t wlanphy_impl_channel = mvmvif_->mlme_channel;

    auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmacIfc>();
    ASSERT_FALSE(endpoints.is_error());

    // Created the end points for WlanSoftmacIfc protocol, and pass the client end to
    // WlanSoftmacDevice.
    fdf::BindServer<fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmacIfc>>(
        server_dispatcher_.get(), std::move(endpoints->server), this);

    // This FIDL call should invoke mac_start() and pass the pointer of WlanSoftmacDeviceTest to it,
    // the pointer will be copied to mvmvif_->ifc.ctx.
    auto result = client_.sync().buffer(test_arena_)->Start(std::move(endpoints->client));

    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result->is_error());
    ASSERT_EQ(wlanphy_impl_channel, result->value()->sme_channel);

    // Add the interface to MVM instance.
    mvmvif_->mvm->mvmvif[0] = mvmvif_;
  }

  ~MacInterfaceTest() {
    VerifyExpectation();  // Ensure all expectations had been met.

    // Restore the original callback for other test cases not using the mock.
    ResetSendCmdFunc();

    // Stop the MAC to free resources we allocated.
    // This must be called after we verify the expected commands and restore the mock command
    // callback so that the stop command doesn't mess up the test case expectation.
    auto result = client_.sync().buffer(test_arena_)->Stop();
    ASSERT_TRUE(result.ok());

    // Deallocate the client station at id 0.
    mtx_lock(&mvmvif_->mvm->mutex);
    iwl_mvm_del_aux_sta(mvmvif_->mvm);
    mtx_unlock(&mvmvif_->mvm->mutex);

    VerifyStaHasBeenRemoved();
  }

  // Used in MockCommand constructor to indicate if the command needs to be either
  //
  //   - returned immediately (with a status code), or
  //   - passed to the sim_mvm.c.
  //
  enum SimMvmBehavior {
    kSimMvmReturnWithStatus,
    kSimMvmBypassToSimMvm,
  };

  // A flexible mock-up of firmware command for testing code. Testing code can decide to either call
  // the simulated firmware or return the status code immediately.
  //
  //   cmd_id: the command ID. Sometimes composed with WIDE_ID() macro.
  //   behavior: determine what this mockup command is to do.
  //   status: the status code to return when behavior is 'kSimMvmReturnWithStatus'.
  //
  class MockCommand {
   public:
    MockCommand(uint32_t cmd_id, SimMvmBehavior behavior, zx_status_t status)
        : cmd_id_(cmd_id), behavior_(behavior), status_(status) {}
    MockCommand(uint32_t cmd_id) : MockCommand(cmd_id, kSimMvmBypassToSimMvm, ZX_OK) {}

    ~MockCommand() {}

    uint32_t cmd_id_;
    SimMvmBehavior behavior_;
    zx_status_t status_;
  };
  typedef std::list<MockCommand> expected_cmd_id_list;
  typedef zx_status_t (*fp_send_cmd)(struct iwl_trans* trans, struct iwl_host_cmd* cmd);

  // Public for MockSendCmd().
  expected_cmd_id_list expected_cmd_ids;
  fp_send_cmd original_send_cmd;

 protected:
  bool IsValidChannel(const fuchsia_wlan_common::wire::WlanChannel* channel) {
    return device_->IsValidChannel(channel);
  }

  zx_status_t SetChannel(const fuchsia_wlan_common::wire::WlanChannel* channel) {
    auto result = client_.sync().buffer(test_arena_)->SetChannel(*channel);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t ConfigureBss(const fuchsia_wlan_internal::wire::BssConfig* config) {
    auto result = client_.sync().buffer(test_arena_)->ConfigureBss(*config);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t ConfigureAssoc(const fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx* ctx) {
    auto result = client_.sync().buffer(test_arena_)->ConfigureAssoc(*ctx);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t ClearAssoc() {
    // Not used since all info were saved in mvmvif_sta_ already.
    fidl::Array<uint8_t, fuchsia_wlan_ieee80211::wire::kMacAddrLen> fidl_peer_addr;
    auto result = client_.sync().buffer(test_arena_)->ClearAssoc(fidl_peer_addr);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t StartPassiveScan(fuchsia_wlan_softmac::wire::WlanSoftmacPassiveScanArgs* args) {
    auto result = client_.sync().buffer(test_arena_)->StartPassiveScan(*args);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t StartActiveScan(fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs* args) {
    auto result = client_.sync().buffer(test_arena_)->StartActiveScan(*args);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t SetKey(const fuchsia_wlan_softmac::wire::WlanKeyConfig* key_config) {
    IWL_INFO(nullptr, "Calling set_key");
    auto result = client_.sync().buffer(test_arena_)->SetKey(*key_config);
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }
    return ZX_OK;
  }

  // The following functions are for mocking up the firmware commands.
  //
  // The mock function will return the special error ZX_ERR_INTERNAL when the expectation
  // is not expected.

  // Set the expected commands sending to the firmware.
  //
  // Args:
  //   cmd_ids: list of expected commands. Will be matched in order.
  //
  void ExpectSendCmd(const expected_cmd_id_list& cmd_ids) {
    expected_cmd_ids = cmd_ids;

    // Re-define the 'dev' field in the 'struct iwl_trans' to a test instance of this class.
    sim_trans_.iwl_trans()->dev = reinterpret_cast<struct device*>(this);

    // Setup the mock function for send command.
    original_send_cmd = sim_trans_.iwl_trans()->ops->send_cmd;
    sim_trans_.iwl_trans()->ops->send_cmd = MockSendCmd;
  }

  // Reset the send command function to the original one, so that the test case would stop checking
  // commands one by one.
  void ResetSendCmdFunc() {
    if (original_send_cmd) {
      IWL_INFO(nullptr, "Reseting send_cmd.");
      sim_trans_.iwl_trans()->ops->send_cmd = original_send_cmd;
    } else {
      IWL_WARN(nullptr, "No original send_cmd found.");
    }
  }

  static zx_status_t MockSendCmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
    MacInterfaceTest* this_ = reinterpret_cast<MacInterfaceTest*>(trans->dev);

    // remove the first one and match.
    expected_cmd_id_list& expected = this_->expected_cmd_ids;
    ZX_ASSERT_MSG(!expected.empty(),
                  "A command (0x%04x) is going to send, but no command is expected.\n", cmd->id);

    // check the command ID.
    auto exp = expected.front();
    ZX_ASSERT_MSG(exp.cmd_id_ == cmd->id,
                  "The command doesn't match! Expect: 0x%04x, actual: 0x%04x.\n", exp.cmd_id_,
                  cmd->id);
    expected.pop_front();

    if (exp.behavior_ == kSimMvmBypassToSimMvm) {
      return this_->original_send_cmd(trans, cmd);
    } else {
      return exp.status_;
    }
  }

  void VerifyExpectation() {
    for (expected_cmd_id_list::iterator it = expected_cmd_ids.begin(); it != expected_cmd_ids.end();
         it++) {
      printf("  ==> 0x%04x\n", it->cmd_id_);
    }
    ASSERT_TRUE(expected_cmd_ids.empty(), "The expected command set is not empty.");

    mock_tx_.VerifyAndClear();
  }

  void VerifyStaHasBeenRemoved() {
    auto mvm = mvmvif_->mvm;

    for (size_t i = 0; i < std::size(mvm->fw_id_to_mac_id); i++) {
      struct iwl_mvm_sta* mvm_sta = mvm->fw_id_to_mac_id[i];
      ASSERT_EQ(nullptr, mvm_sta);
    }
    ASSERT_EQ(0, mvm->vif_count);
  }

  // Mock function for Tx.
  mock_function::MockFunction<zx_status_t,  // return value
                              size_t,       // packet size
                              uint16_t,     // cmd + group_id
                              int           // txq_id
                              >
      mock_tx_;

  static zx_status_t tx_wrapper(struct iwl_trans* trans, struct ieee80211_mac_packet* pkt,
                                const struct iwl_device_cmd* dev_cmd, int txq_id) {
    iwl_stats_inc(IWL_STATS_CNT_DATA_TO_FW);  // to simulate the iwl_trans_pcie_tx() behavior.
    auto test = GET_TEST(MacInterfaceTest, trans);
    return test->mock_tx_.Call(pkt->header_size + pkt->headroom_used_size + pkt->body_size,
                               WIDE_ID(dev_cmd->hdr.group_id, dev_cmd->hdr.cmd), txq_id);
  }

  static constexpr uint8_t kIeeeOui[] = {0x00, 0x0F, 0xAC};
  static constexpr fidl::Array<uint8_t, 32> kFakeKey = {
      .data_ = {1, 2, 3, 4, 5, 6, 7, 8, 0, 9, 8, 7, 6, 5, 4, 3},
  };
  static constexpr size_t kFakeKeyLen = 16;

  static constexpr fidl::Array<uint8_t, 32> kFakeTkipKey = {
      .data_ = {1, 2, 3, 4, 5, 6, 7, 8, 0, 9, 8, 7, 6, 5, 4, 3,
                1, 2, 3, 4, 5, 6, 7, 8, 0, 9, 8, 7, 6, 5, 4, 3},
  };
  static constexpr size_t kFakeTkipKeyLen = 32;

  // Define it's own kChannel and override the one defined in SingleApTest.
  static constexpr fuchsia_wlan_common::wire::WlanChannel kChannel = {
      .primary = 11, .cbw = fuchsia_wlan_common::ChannelBandwidth::kCbw20};

  static constexpr fuchsia_wlan_internal::wire::BssConfig kBssConfig = {
      .bssid =
          {
              .data_ = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
          },
      .bss_type = fuchsia_wlan_internal::wire::BssType::kInfrastructure,
      .remote = true,
  };

  // Assoc context without HT related data.
  static constexpr fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx kAssocCtx = {
      .listen_interval = kListenInterval,
  };

  // Assoc context with HT related data. (The values below comes from real data in manual test)
  static constexpr fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx kHtAssocCtx = {
      .listen_interval = kListenInterval,
      .channel =
          {
              .primary = 157,
              .cbw = fuchsia_wlan_common::ChannelBandwidth::kCbw80,
          },
      .rates_cnt = 8,
      .rates =
          {
              .data_ =
                  {
                      140,
                      18,
                      152,
                      36,
                      176,
                      72,
                      96,
                      108,
                  },
          },
      .has_ht_cap = true,
      .ht_cap =
          {
              .bytes =
                  {
                      .data_ =
                          {
                              0,
                              0,  // HtCapabilityInfo
                              0,  // AmpduParams

                              255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
                              0,  // Supported mcs set

                              0,
                              0,  // HtExtCapabilities
                              0,   0,   0,
                              0,  // TxBeamformingCapabilities
                              0,  // AselCapability
                          },
                  },
          },
  };

  static constexpr uint16_t kChannelSize = 4;
};

TEST_F(MacInterfaceTest, TestIsValidChannel) {
  ExpectSendCmd(expected_cmd_id_list({}));

  fuchsia_wlan_common::wire::WlanChannel ch10_20m = {
      .primary = 10,
      .cbw = fuchsia_wlan_common::ChannelBandwidth::kCbw20,
  };

  EXPECT_TRUE(IsValidChannel(&ch10_20m));

  fuchsia_wlan_common::wire::WlanChannel ch10_40m = {
      .primary = 10,
      .cbw = fuchsia_wlan_common::ChannelBandwidth::kCbw40,
  };

  EXPECT_FALSE(IsValidChannel(&ch10_40m));

  fuchsia_wlan_common::wire::WlanChannel ch13_40m_below = {
      .primary = 13,
      .cbw = fuchsia_wlan_common::ChannelBandwidth::kCbw40Below,
  };

  EXPECT_FALSE(IsValidChannel(&ch13_40m_below));

  fuchsia_wlan_common::wire::WlanChannel ch5_80m = {
      .primary = 5,
      .cbw = fuchsia_wlan_common::ChannelBandwidth::kCbw80,
  };

  EXPECT_FALSE(IsValidChannel(&ch5_80m));
}

// Test the set_channel().
//
TEST_F(MacInterfaceTest, TestSetChannel) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),
  }));

  mvmvif_->csa_bcn_pending = true;  // Expect to be clear because this is client role.
  ASSERT_OK(SetChannel(&kChannel));
  EXPECT_FALSE(mvmvif_->csa_bcn_pending);
}

// Test call set_channel() multiple times.
//
TEST_F(MacInterfaceTest, TestMultipleSetChannel) {
  ExpectSendCmd(expected_cmd_id_list({
      // for the first SetChannel()
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),

      // for the second SetChannel()
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),  // for remove_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),
  }));

  for (size_t i = 0; i < 2; i++) {
    ASSERT_OK(SetChannel(&kChannel));
  }
}

// Test the unsupported MAC role.
//
TEST_F(MacInterfaceTest, TestSetChannelWithUnsupportedRole) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
  }));

  mvmvif_->mac_role = WLAN_MAC_ROLE_AP;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, SetChannel(&kChannel));
}

// Test ConfigureBss()
//
TEST_F(MacInterfaceTest, TestConfigureBss) {
  ASSERT_OK(SetChannel(&kChannel));

  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, TIME_EVENT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
      MockCommand(WIDE_ID(LONG_GROUP, SCD_QUEUE_CFG)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
  }));

  ASSERT_OK(ConfigureBss(&kBssConfig));
  // Ensure the BSSID was copied into mvmvif
  ASSERT_EQ(memcmp(mvmvif_->bss_conf.bssid, kBssConfig.bssid.data(), ETH_ALEN), 0);
  ASSERT_EQ(memcmp(mvmvif_->bssid, kBssConfig.bssid.data(), ETH_ALEN), 0);
}

// Test duplicate BSS config.
//
TEST_F(MacInterfaceTest, DuplicateConfigureBss) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, ConfigureBss(&kBssConfig));
}

// Test unsupported bss_type.
//
TEST_F(MacInterfaceTest, UnsupportedBssType) {
  static constexpr fuchsia_wlan_internal::wire::BssConfig kUnsupportedBssConfig = {
      .bssid =
          {
              .data_ = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
          },
      .bss_type = fuchsia_wlan_internal::wire::BssType::kIndependent,
      .remote = true,
  };
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ConfigureBss(&kUnsupportedBssConfig));
}

// Test failed ADD_STA command.
//
TEST_F(MacInterfaceTest, TestFailedAddSta) {
  ASSERT_OK(SetChannel(&kChannel));

  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD), kSimMvmReturnWithStatus,
                  ZX_ERR_BUFFER_TOO_SMALL /* an arbitrary error */),
  }));

  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, ConfigureBss(&kBssConfig));
}

// Test whether the AUX sta (for active scan) is added.
//
TEST_F(MacInterfaceTest, TestAuxSta) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));

  auto aux_sta = &mvmvif_->mvm->aux_sta;
  EXPECT_EQ(IWL_STA_AUX_ACTIVITY, aux_sta->type);
  EXPECT_NE(IWL_MVM_INVALID_STA, aux_sta->sta_id);
  EXPECT_NE(nullptr, mvmvif_->mvm->fw_id_to_mac_id[0]);  // Assume it is the first one.

  // The removal check is done in VerifyStaHasBeenRemoved() of MacInterfaceTest deconstructor.
}

// Test exception handling in driver.
//
TEST_F(MacInterfaceTest, TestExceptionHandling) {
  ASSERT_OK(SetChannel(&kChannel));

  // Test the beacon interval checking.
  mvmvif_->bss_conf.beacon_int = 0;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ConfigureBss(&kBssConfig));
  mvmvif_->bss_conf.beacon_int = 16;  // which just passes the check.

  // Test the phy_ctxt checking.
  auto backup_phy_ctxt = mvmvif_->phy_ctxt;
  mvmvif_->phy_ctxt = nullptr;
  EXPECT_EQ(ZX_ERR_BAD_STATE, ConfigureBss(&kBssConfig));
  mvmvif_->phy_ctxt = backup_phy_ctxt;

  iwl_mvm_sta sta;
  // Test the case we run out of slots for STA.
  // Occupy all the station slots.
  for (uint16_t i = 0; i < IWL_MVM_STATION_COUNT; i++) {
    mvmvif_->mvm->fw_id_to_mac_id[i] = &sta;
  }

  // Request fails because we run out of all slots in fw_id_to_mac_id[].
  EXPECT_EQ(ZX_ERR_NO_RESOURCES, ConfigureBss(&kBssConfig));

  // Clean up all the station slots.
  for (uint16_t i = 0; i < IWL_MVM_STATION_COUNT; i++) {
    mvmvif_->mvm->fw_id_to_mac_id[i] = nullptr;
  }

  EXPECT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  // iwl_mvm_add_sta(), called by ConfigureBss(), has an assumption that each interface has only one
  // AP sta (for WLAN_MAC_ROLE_CLIENT). However, in this case, we break the assumption so that the
  // ap_sta_id was populated with the last successful STA ID. Thus, we reset the mvmvif_->ap_sta_id
  // so that the SoftMacDevices destructor will not release the resource twice by calling
  // ClearAssoc().
  mvmvif_->ap_sta_id = IWL_MVM_INVALID_STA;
}

// The test is used to test the typical procedure to connect to an open network.
//
TEST_F(MacInterfaceTest, AssociateToOpenNetwork) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_OK(ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_TRUE(mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);
  ASSERT_EQ(mvm_sta->sta_state, iwl_sta_state::IWL_STA_AUTHORIZED);

  ASSERT_OK(ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
}

// Check if calling iwl_mvm_mac_sta_state() sets the state correctly.
TEST_F(MacInterfaceTest, CheckStaState) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_OK(ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_TRUE(mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);
  ASSERT_EQ(mvm_sta->sta_state, iwl_sta_state::IWL_STA_AUTHORIZED);

  ASSERT_EQ(ZX_OK, iwl_mvm_mac_sta_state(mvmvif_, mvm_sta, IWL_STA_AUTHORIZED, IWL_STA_ASSOC));
  ASSERT_EQ(mvm_sta->sta_state, iwl_sta_state::IWL_STA_ASSOC);
  ASSERT_OK(ClearAssoc());
}

// Back to back calls of ClearAssoc().
TEST_F(MacInterfaceTest, ClearAssocAfterClearAssoc) {
  ASSERT_NE(ZX_OK, ClearAssoc());
  ASSERT_NE(ZX_OK, ClearAssoc());
}

// ClearAssoc() should cleanup when called without Assoc
TEST_F(MacInterfaceTest, ClearAssocAfterNoAssoc) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_OK(ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);

  // Call ClearAssoc() again to check if it is handled correctly.
  ASSERT_NE(ZX_OK, ClearAssoc());
}

// ClearAssoc() should cleanup when called after a failed Assoc
TEST_F(MacInterfaceTest, ClearAssocAfterFailedAssoc) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));

  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);
  // Fail the association by forcing some relevant internal state.
  auto orig = mvmvif_->uploaded;
  mvmvif_->uploaded = false;
  ASSERT_EQ(ZX_ERR_IO, ConfigureAssoc(&kAssocCtx));
  mvmvif_->uploaded = orig;

  // ClearAssoc will clean up the failed association.
  ASSERT_OK(ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);

  // Call ClearAssoc() again to check if it is handled correctly.
  ASSERT_NE(ZX_OK, ClearAssoc());
}

// This test case is to verify ConfigureAssoc() with HT wlan_assoc_ctx_t input can successfully
// trigger LQ_CMD with correct data.
TEST_F(MacInterfaceTest, AssocWithHtConfig) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));

  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, LQ_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, TIME_EVENT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MCAST_FILTER_CMD)),
  }));

  // Extract LQ_CMD data.
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  struct iwl_lq_cmd* lq_cmd = &mvm_sta->lq_sta.rs_drv.lq;

  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_OK(ConfigureAssoc(&kHtAssocCtx));

  // Verify the values in LQ_CMD API structure.
  EXPECT_EQ(lq_cmd->sta_id, 0);
  EXPECT_EQ(lq_cmd->reduced_tpc, 0);
  EXPECT_EQ(lq_cmd->flags, 0);
  EXPECT_EQ(lq_cmd->mimo_delim, 0);
  EXPECT_EQ(lq_cmd->single_stream_ant_msk, 1);
  EXPECT_EQ(lq_cmd->dual_stream_ant_msk, 3);
  EXPECT_EQ(lq_cmd->initial_rate_index[0], 0);
  EXPECT_EQ(lq_cmd->initial_rate_index[1], 0);
  EXPECT_EQ(lq_cmd->initial_rate_index[2], 0);
  EXPECT_EQ(lq_cmd->initial_rate_index[3], 0);
  EXPECT_EQ(lq_cmd->agg_time_limit, 0x0fa0);
  EXPECT_EQ(lq_cmd->agg_disable_start_th, 3);
  EXPECT_EQ(lq_cmd->agg_frame_cnt_limit, 1);
  EXPECT_EQ(lq_cmd->reserved2, 0);

  // Verify rate_n_flags in the table.
  EXPECT_EQ(lq_cmd->rs_table[0], 0x4103);
  // The value of RS_MNG_RETRY_TABLE_INITIAL_RATE_NUM is 3.
  EXPECT_EQ(lq_cmd->rs_table[3], 0x4102);

  EXPECT_EQ(lq_cmd->ss_params, 0);

  // Stop checking following commands one by one.
  ResetSendCmdFunc();

  // Clean up the association states.
  ASSERT_OK(ClearAssoc());
}

TEST_F(MacInterfaceTest, StartPassiveScanTest) {
  fidl::Arena fidl_arena;
  // FromExternal() is not able to take const data.
  uint8_t channels_to_scan[kChannelSize] = {7, 1, 40, 136};
  {
    // Passive scan with some random channels should pass.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacPassiveScanArgs::Builder(fidl_arena);
    builder.channels(fidl::VectorView<uint8_t>::FromExternal(&channels_to_scan[0], kChannelSize));
    auto passive_scan_args = builder.Build();
    ASSERT_OK(StartPassiveScan(&passive_scan_args));
  }

  {
    // Passive scan request will fail in mvm-mlme.cc if the channels field is not set.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacPassiveScanArgs::Builder(fidl_arena);
    auto passive_scan_args = builder.Build();
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartPassiveScan(&passive_scan_args));
  }
}

TEST_F(MacInterfaceTest, StartActiveScanTest) {
  fidl::Arena fidl_arena;
  // FromExternal() is not able to take const data.
  uint8_t channels_to_scan[kChannelSize] = {7, 1, 40, 136};
  {
    // Active scan with args in which all of "channels", "ssids", "mac_header" and "ies" fields are
    // set will pass the argument check in mvm-mlme.cc.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs::Builder(fidl_arena);

    builder.channels(fidl::VectorView<uint8_t>::FromExternal(&channels_to_scan[0], kChannelSize));
    builder.ssids(fidl::VectorView<fuchsia_wlan_ieee80211::wire::CSsid>());
    builder.mac_header(fidl::VectorView<uint8_t>());
    builder.ies(fidl::VectorView<uint8_t>());

    auto active_scan_args = builder.Build();
    ASSERT_OK(StartActiveScan(&active_scan_args));
  }

  {
    // Missing "channels" fails the argument check.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs::Builder(fidl_arena);

    builder.ssids(fidl::VectorView<fuchsia_wlan_ieee80211::wire::CSsid>());
    builder.mac_header(fidl::VectorView<uint8_t>());
    builder.ies(fidl::VectorView<uint8_t>());

    auto active_scan_args = builder.Build();
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartActiveScan(&active_scan_args));
  }

  {
    // Missing "ssids" fails the argument check.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs::Builder(fidl_arena);

    builder.channels(fidl::VectorView<uint8_t>::FromExternal(&channels_to_scan[0], kChannelSize));
    builder.mac_header(fidl::VectorView<uint8_t>());
    builder.ies(fidl::VectorView<uint8_t>());

    auto active_scan_args = builder.Build();
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartActiveScan(&active_scan_args));
  }

  {
    // Missing "mac_header" fails the argument check.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs::Builder(fidl_arena);

    builder.channels(fidl::VectorView<uint8_t>::FromExternal(&channels_to_scan[0], kChannelSize));
    builder.ssids(fidl::VectorView<fuchsia_wlan_ieee80211::wire::CSsid>());
    builder.ies(fidl::VectorView<uint8_t>());

    auto active_scan_args = builder.Build();
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartActiveScan(&active_scan_args));
  }

  {
    // Missing "ies" fails the argument check.
    auto builder = fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs::Builder(fidl_arena);

    builder.channels(fidl::VectorView<uint8_t>::FromExternal(&channels_to_scan[0], kChannelSize));
    builder.ssids(fidl::VectorView<fuchsia_wlan_ieee80211::wire::CSsid>());
    builder.mac_header(fidl::VectorView<uint8_t>());

    auto active_scan_args = builder.Build();
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartActiveScan(&active_scan_args));
  }
}

// Check to ensure keys are set during assoc and deleted after disassoc
// for now use open network
TEST_F(MacInterfaceTest, SetKeysTest) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_OK(ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_TRUE(mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);

  fidl::Arena fidl_arena;
  fidl::Array<uint8_t, 3> cipher_oui;
  auto key =
      fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(kFakeKey.begin()), kFakeKeyLen);
  memcpy(cipher_oui.begin(), kIeeeOui, 3);
  {
    auto builder = fuchsia_wlan_softmac::wire::WlanKeyConfig::Builder(fidl_arena);

    // Set an arbitrary pairwise key.
    builder.cipher_type(4);
    builder.key_type(fuchsia_hardware_wlan_associnfo::wire::WlanKeyType::kPairwise);
    builder.key_idx(0);
    builder.key(key);
    builder.rsc(0);
    builder.cipher_oui(cipher_oui);
    auto key_config = builder.Build();
    ASSERT_OK(SetKey(&key_config));
    // Expect bit 0 to be set.
    ASSERT_EQ(*mvm->fw_key_table, 0x1);
  }
  {
    auto builder = fuchsia_wlan_softmac::wire::WlanKeyConfig::Builder(fidl_arena);

    // Set an arbitrary group key.
    builder.cipher_type(4);
    builder.key_type(fuchsia_hardware_wlan_associnfo::wire::WlanKeyType::kGroup);
    builder.key_idx(1);
    builder.key(key);
    builder.rsc(0);
    builder.cipher_oui(cipher_oui);

    auto key_config = builder.Build();
    ASSERT_OK(SetKey(&key_config));
    // Expect bit 1 to be set as well.
    ASSERT_EQ(*mvm->fw_key_table, 0x3);
  }

  ASSERT_OK(ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
  // Both the keys should have been deleted.
  ASSERT_EQ(*mvm->fw_key_table, 0x0);
}

// Check that we can sucessfully set some key configurations required for supported functionality.
TEST_F(MacInterfaceTest, SetKeysSupportConfigs) {
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  ASSERT_OK(ConfigureAssoc(&kAssocCtx));
  ASSERT_TRUE(mvmvif_->bss_conf.assoc);

  fidl::Arena fidl_arena;
  fidl::Array<uint8_t, 3> cipher_oui;
  auto key =
      fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(kFakeKey.begin()), kFakeKeyLen);
  memcpy(cipher_oui.begin(), kIeeeOui, 3);
  {
    auto builder = fuchsia_wlan_softmac::wire::WlanKeyConfig::Builder(fidl_arena);

    builder.key(key);
    builder.cipher_oui(cipher_oui);
    // Default cipher configuration for WPA2/3 PTK.  This is data frame protection, required for
    // WPA2/3.
    builder.cipher_type(CIPHER_SUITE_TYPE_CCMP_128);
    builder.key_type(fuchsia_hardware_wlan_associnfo::wire::WlanKeyType::kPairwise);
    builder.key_idx(0);
    builder.rsc(0);
    auto key_config = builder.Build();
    ASSERT_OK(SetKey(&key_config));
  }

  {
    auto builder = fuchsia_wlan_softmac::wire::WlanKeyConfig::Builder(fidl_arena);

    // Default cipher configuration for WPA2/3 IGTK.  This is management frame protection, optional
    // for WPA2 and required for WPA3.
    builder.key(key);
    builder.cipher_oui(cipher_oui);
    builder.cipher_type(CIPHER_SUITE_TYPE_BIP_CMAC_128);
    builder.key_type(fuchsia_hardware_wlan_associnfo::wire::WlanKeyType::kIgtk);
    builder.key_idx(1);
    builder.rsc(0);
    auto key_config = builder.Build();
    ASSERT_OK(SetKey(&key_config));
  }

  ASSERT_OK(ClearAssoc());
}

// Test setting TKIP. Mainly for group key (backward-compatible for many APs).
TEST_F(MacInterfaceTest, SetKeysTKIP) {
  constexpr uint8_t kIeeeOui[] = {0x00, 0x0F, 0xAC};
  ASSERT_OK(SetChannel(&kChannel));
  ASSERT_OK(ConfigureBss(&kBssConfig));
  ASSERT_OK(ConfigureAssoc(&kAssocCtx));
  ASSERT_TRUE(mvmvif_->bss_conf.assoc);

  fidl::Arena fidl_arena;
  fidl::Array<uint8_t, 3> cipher_oui;
  auto tkip_key = fidl::VectorView<uint8_t>::FromExternal(
      const_cast<uint8_t*>(kFakeTkipKey.begin()), kFakeTkipKeyLen);
  memcpy(cipher_oui.begin(), kIeeeOui, 3);
  {
    auto builder = fuchsia_wlan_softmac::wire::WlanKeyConfig::Builder(fidl_arena);

    builder.key(tkip_key);
    builder.cipher_oui(cipher_oui);
    // TKIP Pairwise: although we support it but not recommended (deprecated protocol).
    builder.cipher_type(fidl::ToUnderlying(fuchsia_wlan_ieee80211::wire::CipherSuiteType::kTkip));
    builder.key_type(fuchsia_hardware_wlan_associnfo::wire::WlanKeyType::kPairwise);
    builder.key_idx(0);
    builder.rsc(0);
    auto key_config = builder.Build();
    ASSERT_OK(SetKey(&key_config));
  }

  {
    // TKIP Group key: supported for backward compatible. Unfortunately many APs still use this.
    auto builder = fuchsia_wlan_softmac::wire::WlanKeyConfig::Builder(fidl_arena);

    builder.key(tkip_key);
    builder.cipher_oui(cipher_oui);
    builder.cipher_type(fidl::ToUnderlying(fuchsia_wlan_ieee80211::wire::CipherSuiteType::kTkip));
    builder.key_type(fuchsia_hardware_wlan_associnfo::wire::WlanKeyType::kIgtk);
    builder.key_idx(1);
    builder.rsc(0);
    auto key_config = builder.Build();
    ASSERT_OK(SetKey(&key_config));
  }

  ASSERT_OK(ClearAssoc());
}

TEST_F(MacInterfaceTest, TxPktTooLong) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build_oversize();
  fuchsia_wlan_softmac::wire::WlanTxPacket fidl_packet = wlan_pkt->wlan_pkt();

  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_FROM_MLME), 0);
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_TO_FW), 0);

  auto result = client_.sync().buffer(test_arena_)->QueueTx(fidl_packet);
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_FROM_MLME), 1);
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_TO_FW), 0);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->is_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result->error_value());
  unbindTx();
}

TEST_F(MacInterfaceTest, TxPktNotSupportedRole) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  // Set to an unsupported role.
  mvmvif_->mac_role = WLAN_MAC_ROLE_AP;

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build();
  fuchsia_wlan_softmac::wire::WlanTxPacket fidl_packet = wlan_pkt->wlan_pkt();

  auto result = client_.sync().buffer(test_arena_)->QueueTx(fidl_packet);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->is_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, result->error_value());
  unbindTx();
}

// To test if a packet can be sent out.
TEST_F(MacInterfaceTest, TxPkt) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build();
  mock_tx_.ExpectCall(ZX_OK, wlan_pkt->len(), WIDE_ID(0, TX_CMD), IWL_MVM_DQA_MIN_MGMT_QUEUE);
  fuchsia_wlan_softmac::wire::WlanTxPacket fidl_packet = wlan_pkt->wlan_pkt();

  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_FROM_MLME), 0);
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_TO_FW), 0);

  auto result = client_.sync().buffer(test_arena_)->QueueTx(fidl_packet);
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_FROM_MLME), 1);
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_DATA_TO_FW), 1);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->is_error());
  ASSERT_FALSE(result->value()->enqueue_pending);
  unbindTx();
}

}  // namespace
}  // namespace wlan::testing
