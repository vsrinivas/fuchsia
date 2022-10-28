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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/client_connection.h"

#include <lib/async/cpp/task.h>
#include <lib/sync/completion.h>
#include <netinet/ether.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/key_ring.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

using wlan::nxpfmac::ClientConnection;
using wlan::nxpfmac::ClientConnectionIfc;
using wlan::nxpfmac::Device;
using wlan::nxpfmac::DeviceContext;

constexpr uint8_t kIesWithSsid[] = {"\x00\x04Test"};
constexpr uint8_t kTestChannel = 6;
constexpr uint32_t kTestBssIndex = 3;
constexpr wlan_fullmac_connect_req_t kMinimumConnectReq = {
    .selected_bss{.ies_list = kIesWithSsid,
                  .ies_count = sizeof(kIesWithSsid),
                  .channel{.primary = kTestChannel}},
    .auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM};

// Barebones Device Class (at this time mainly for the dispatcher to handle timers)
struct TestDevice : public Device {
 public:
  static zx_status_t Create(zx_device_t *parent, async_dispatcher_t *dispatcher,
                            sync_completion_t *destruction_compl, TestDevice **out_device) {
    auto device = new TestDevice(parent, dispatcher, destruction_compl);

    *out_device = device;
    return ZX_OK;
  }
  ~TestDevice() {}

  async_dispatcher_t *GetDispatcher() override { return dispatcher_; }

 private:
  TestDevice(zx_device_t *parent, async_dispatcher_t *dispatcher,
             sync_completion_t *destructor_done)
      : Device(parent) {
    dispatcher_ = dispatcher;
  }

 protected:
  zx_status_t Init(mlan_device *mlan_dev, wlan::nxpfmac::BusInterface **out_bus) override {
    return ZX_OK;
  }
  zx_status_t LoadFirmware(const char *path, zx::vmo *out_fw, size_t *out_size) override {
    return ZX_OK;
  }
  void Shutdown() override {}

  async_dispatcher_t *dispatcher_;
  wlan::nxpfmac::DeviceContext context_;
  wlan::nxpfmac::MockBus bus_;
};

class TestClientConnectionIfc : public ClientConnectionIfc {
  void OnDisconnectEvent(uint16_t reason_code) override {}
  void SignalQualityIndication(int8_t rssi, int8_t snr) override {}
};

struct ClientConnectionTest : public zxtest::Test {
  void SetUp() override {
    event_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
    event_loop_->StartThread();
    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    key_ring_ = std::make_unique<wlan::nxpfmac::KeyRing>(ioctl_adapter_.get(), kTestBssIndex);
    parent_ = MockDevice::FakeRootParent();
    ASSERT_OK(TestDevice::Create(parent_.get(), env_.GetDispatcher(), &device_destructed_,
                                 &test_device_));

    context_ = wlan::nxpfmac::DeviceContext{.device_ = test_device_,
                                            .event_handler_ = &event_handler_,
                                            .ioctl_adapter_ = ioctl_adapter_.get()};
  }

  void TearDown() override {
    delete context_.device_;
    context_.device_ = nullptr;
    event_loop_->Shutdown();
  }

  mlan_status HandleOtherIoctls(pmlan_ioctl_req req) {
    if (req->req_id == MLAN_IOCTL_SEC_CFG || req->req_id == MLAN_IOCTL_MISC_CFG ||
        req->req_id == MLAN_IOCTL_GET_INFO) {
      // Connecting performs security and IE (MISC ioctl) configuration, make sure it succeeds.
      return MLAN_STATUS_SUCCESS;
    }
    if (req->req_id == MLAN_IOCTL_SCAN) {
      // For scans we must send a scan report to continue the scanning process.
      zxlogf(INFO, "Posting event");
      async::PostTask(event_loop_->dispatcher(), [this]() {
        zxlogf(INFO, "Sending event");
        mlan_event event{.event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
        event_handler_.OnEvent(&event);
      });
      return MLAN_STATUS_SUCCESS;
    }
    if (req->req_id == MLAN_IOCTL_BSS) {
      auto request = reinterpret_cast<wlan::nxpfmac::IoctlRequest<mlan_ds_bss> *>(req);
      if (request->UserReq().sub_command == MLAN_OID_BSS_CHANNEL_LIST) {
        constexpr uint8_t kChannels[] = {1,   2,   3,   4,   5,   6,   7,   8,   9,
                                         10,  11,  36,  40,  44,  48,  52,  56,  60,
                                         64,  100, 104, 108, 112, 116, 120, 124, 128,
                                         132, 136, 140, 144, 149, 153, 157, 161, 165};
        for (size_t i = 0; i < std::size(kChannels); ++i) {
          request->UserReq().param.chanlist.cf[i].channel = kChannels[i];
        }
        request->UserReq().param.chanlist.num_of_chan = std::size(kChannels);

        return MLAN_STATUS_SUCCESS;
      }
    }
    // Unexpected
    ADD_FAILURE("Should not reach this point, unexpected ioctl 0x%x", req->req_id);
    return MLAN_STATUS_FAILURE;
  }

  std::unique_ptr<async::Loop> event_loop_;
  wlan::simulation::Environment env_ = {};
  TestDevice *test_device_ = nullptr;
  wlan::nxpfmac::MlanMockAdapter mocks_;
  wlan::nxpfmac::MockBus mock_bus_;
  wlan::nxpfmac::EventHandler event_handler_;
  wlan::nxpfmac::DeviceContext context_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
  std::unique_ptr<wlan::nxpfmac::KeyRing> key_ring_;
  TestClientConnectionIfc test_ifc_;
  sync_completion_t device_destructed_;
  std::shared_ptr<MockDevice> parent_;
};

TEST_F(ClientConnectionTest, Constructible) {
  ASSERT_NO_FATAL_FAILURE(ClientConnection(&test_ifc_, &context_, nullptr, 0));
}

TEST_F(ClientConnectionTest, Connect) {
  constexpr uint8_t kBss[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  constexpr uint8_t kStaAddr[] = {0x0e, 0x0d, 0x16, 0x28, 0x3a, 0x4c};
  constexpr uint32_t kBssIndex = 0;
  constexpr int8_t kTestRssi = -64;
  constexpr int8_t kTestSnr = 28;
  constexpr zx::duration kSignalLogTimeout = zx::sec(30);

  wlan_fullmac_connect_req_t request = kMinimumConnectReq;
  memcpy(request.selected_bss.bssid, kBss, sizeof(kBss));

  sync_completion_t ioctl_completion;

  class TestClientConnectionIfc : public ClientConnectionIfc {
    void OnDisconnectEvent(uint16_t reason_code) override {}
    void SignalQualityIndication(int8_t rssi, int8_t snr) override {
      ind_rssi = rssi;
      ind_snr = snr;
    }

   public:
    int8_t get_ind_rssi() { return ind_rssi; }

    int8_t get_ind_snr() { return ind_snr; }

    int8_t ind_rssi = 0;
    int8_t ind_snr = 0;
  };

  TestClientConnectionIfc test_ifc;
  sync_completion_t signal_ioctl_received;
  std::atomic<bool> ies_cleared;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the actual connect ioctl.

        auto request = reinterpret_cast<wlan::nxpfmac::IoctlRequest<mlan_ds_bss> *>(req);
        auto &user_req = request->UserReq();
        EXPECT_EQ(MLAN_IOCTL_BSS, request->IoctlReq().req_id);
        EXPECT_EQ(MLAN_ACT_SET, request->IoctlReq().action);

        // EXPECT_EQ(MLAN_OID_BSS_START, user_req.sub_command);
        EXPECT_EQ(kBssIndex, user_req.param.ssid_bssid.idx);
        EXPECT_EQ(kTestChannel, user_req.param.ssid_bssid.channel);
        EXPECT_BYTES_EQ(kBss, user_req.param.ssid_bssid.bssid, ETH_ALEN);

        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);

        sync_completion_signal(&ioctl_completion);
        return MLAN_STATUS_PENDING;
      }
      if (bss->sub_command == MLAN_OID_BSS_STOP) {
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
    } else if (req->action == MLAN_ACT_GET && req->req_id == MLAN_IOCTL_GET_INFO) {
      auto signal_info = reinterpret_cast<mlan_ds_get_info *>(req->pbuf);
      if (signal_info->sub_command == MLAN_OID_GET_SIGNAL) {
        signal_info->param.signal.data_snr_avg = kTestSnr;
        signal_info->param.signal.data_rssi_avg = kTestRssi;
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        sync_completion_signal(&signal_ioctl_received);
        return MLAN_STATUS_PENDING;
      }
    } else if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_MISC_CFG) {
      auto misc_cfg = reinterpret_cast<mlan_ds_misc_cfg *>(req->pbuf);
      if (misc_cfg->sub_command == MLAN_OID_MISC_GEN_IE) {
        if (misc_cfg->param.gen_ie.type == MLAN_IE_TYPE_GEN_IE && misc_cfg->param.gen_ie.len == 0) {
          // This is an ioctl to clear out any existing IEs.
          ies_cleared = true;
        }
      }
    }
    return HandleOtherIoctls(req);
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  ClientConnection connection(&test_ifc, &context_, key_ring_.get(), kBssIndex);

  sync_completion_t connect_completion;
  auto on_connect = [&](ClientConnection::StatusCode status_code, const uint8_t *ies,
                        size_t ies_size) {
    EXPECT_EQ(ClientConnection::StatusCode::kSuccess, status_code);
    sync_completion_signal(&connect_completion);
  };

  ASSERT_OK(connection.Connect(&request, std::move(on_connect)));

  ASSERT_OK(sync_completion_wait(&ioctl_completion, ZX_TIME_INFINITE));
  ASSERT_OK(sync_completion_wait(&connect_completion, ZX_TIME_INFINITE));

  // Wait until the timer has been scheduled.
  while (1) {
    if (env_.GetLatestEventTime().get() >= kSignalLogTimeout.to_nsecs()) {
      break;
    }
  }
  // Let the timer run
  env_.Run(kSignalLogTimeout);
  ASSERT_OK(sync_completion_wait(&signal_ioctl_received, ZX_TIME_INFINITE));
  // Ensure the Signal quality indication was called.
  EXPECT_EQ(test_ifc.ind_rssi, kTestRssi);
  EXPECT_EQ(test_ifc.ind_snr, kTestSnr);
  // Expect that each connect call will clear out any existing IEs first.
  EXPECT_TRUE(ies_cleared.load());

  ASSERT_OK(connection.Disconnect(kStaAddr, 0, [&](wlan::nxpfmac::IoctlStatus status) {
    EXPECT_EQ(wlan::nxpfmac::IoctlStatus::Success, status);
  }));
}

TEST_F(ClientConnectionTest, CancelConnect) {
  constexpr uint32_t kBssIndex = 0;

  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the actual connect call, don't indicate ioctl complete and return pending so
        // that the connect attempt is never completed, allowing us to cancel it with certainty.
        return MLAN_STATUS_PENDING;
      }
    }
    if (req->action == MLAN_ACT_CANCEL) {
      // The cancelation also needs to set the status code and call ioctl complete with a failure.
      req->status_code = MLAN_ERROR_CMD_CANCEL;
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Failure);
      return MLAN_STATUS_SUCCESS;
    }
    return HandleOtherIoctls(req);
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  ClientConnection connection(&test_ifc_, &context_, key_ring_.get(), kBssIndex);

  wlan_fullmac_connect_req_t request = kMinimumConnectReq;

  sync_completion_t completion;
  ASSERT_OK(connection.Connect(
      &request, [&](ClientConnection::StatusCode status_code, const uint8_t *, size_t) {
        EXPECT_EQ(ClientConnection::StatusCode::kCanceled, status_code);
        sync_completion_signal(&completion);
      }));

  ASSERT_OK(connection.CancelConnect());
  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

TEST_F(ClientConnectionTest, Disconnect) {
  // Test that disconnecting works as expected.
  constexpr uint8_t kStaAddr[] = {0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c};
  constexpr uint16_t kReasonCode = 12;

  std::atomic<int> connect_calls = 0;
  std::atomic<int> disconnect_calls = 0;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the connect call. Complete it asynchronously.
        ++connect_calls;
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
      if (bss->sub_command == MLAN_OID_BSS_STOP) {
        // This is the cancel call. Complete it asynchronously.
        ++disconnect_calls;
        // Make sure the reason code propagated correctly.
        EXPECT_EQ(kReasonCode,
                  reinterpret_cast<const mlan_ds_bss *>(req->pbuf)->param.deauth_param.reason_code);
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
    }
    return HandleOtherIoctls(req);
  };
  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  ClientConnection connection(&test_ifc_, &context_, key_ring_.get(), kTestBssIndex);

  // First ensure that if we're not connected we can't disconnect.
  ASSERT_EQ(ZX_ERR_NOT_CONNECTED, connection.Disconnect(kStaAddr, kReasonCode, [](auto) {}));

  wlan_fullmac_connect_req_t request = kMinimumConnectReq;

  sync_completion_t connect_completion;
  auto on_connect = [&](ClientConnection::StatusCode status, const uint8_t *ies, size_t ies_len) {
    EXPECT_EQ(ClientConnection::StatusCode::kSuccess, status);
    sync_completion_signal(&connect_completion);
  };

  // Now connect so that we can successfully disconnect
  ASSERT_OK(connection.Connect(&request, std::move(on_connect)));
  ASSERT_OK(sync_completion_wait(&connect_completion, ZX_TIME_INFINITE));

  // Disconnecting should now work.
  sync_completion_t disconnect_completion;
  ASSERT_OK(connection.Disconnect(kStaAddr, kReasonCode, [&](wlan::nxpfmac::IoctlStatus status) {
    EXPECT_EQ(wlan::nxpfmac::IoctlStatus::Success, status);
    sync_completion_signal(&disconnect_completion);
  }));

  sync_completion_wait(&disconnect_completion, ZX_TIME_INFINITE);

  // Now that we're successfully disconnected make sure disconnect fails again.
  ASSERT_EQ(ZX_ERR_NOT_CONNECTED, connection.Disconnect(kStaAddr, kReasonCode, [](auto) {}));

  // These calls should only have happened once
  ASSERT_EQ(1u, connect_calls.load());
  ASSERT_EQ(1u, disconnect_calls.load());
}

TEST_F(ClientConnectionTest, DisconnectOnDestruct) {
  // Test that Disconnect is called when a connection object is destroyed.

  constexpr uint16_t kReasonCode = REASON_CODE_LEAVING_NETWORK_DEAUTH;

  std::atomic<bool> disconnect_called = false;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the connect call. Complete it asynchronously.
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
      if (bss->sub_command == MLAN_OID_BSS_STOP) {
        // This is the cancel call. Complete it asynchronously.
        // Make sure the correct reason code is used.
        auto &deauth = reinterpret_cast<const mlan_ds_bss *>(req->pbuf)->param.deauth_param;
        EXPECT_EQ(kReasonCode, deauth.reason_code);
        // And that an empty MAC address was indicated.
        EXPECT_BYTES_EQ("\0\0\0\0\0\0", deauth.mac_addr, ETH_ALEN);
        disconnect_called = true;
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
    }
    return HandleOtherIoctls(req);
  };
  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  {
    ClientConnection connection(&test_ifc_, &context_, key_ring_.get(), kTestBssIndex);

    wlan_fullmac_connect_req_t request = kMinimumConnectReq;

    sync_completion_t connect_completion;
    auto on_connect = [&](ClientConnection::StatusCode status, const uint8_t *ies, size_t ies_len) {
      EXPECT_EQ(ClientConnection::StatusCode::kSuccess, status);
      sync_completion_signal(&connect_completion);
    };

    // First we connect so that we can successfully disconnect
    ASSERT_OK(connection.Connect(&request, std::move(on_connect)));
    ASSERT_OK(sync_completion_wait(&connect_completion, ZX_TIME_INFINITE));
  }
  // The ClientConnection object has now gone out of scope and should have disconnected as part of
  // being destroyed.
  ASSERT_TRUE(disconnect_called.load());
}

TEST_F(ClientConnectionTest, DisconnectAsyncFailure) {
  // Test that if the disconnect ioctl fails asynchronously we get the correct status code.
  constexpr uint8_t kStaAddr[] = {0x0e, 0x0d, 0x16, 0x28, 0x3a, 0x4c};
  constexpr uint16_t kReasonCode = 2;

  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the connect call. Complete it asynchronously.
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
      if (bss->sub_command == MLAN_OID_BSS_STOP) {
        // This is the disconnect call. Fail it asynchronously.
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Failure);
        return MLAN_STATUS_PENDING;
      }
    }
    return HandleOtherIoctls(req);
  };
  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  ClientConnection connection(&test_ifc_, &context_, key_ring_.get(), kTestBssIndex);

  sync_completion_t connect_completion;
  auto on_connect = [&](ClientConnection::StatusCode status, const uint8_t *ies, size_t ies_len) {
    EXPECT_EQ(ClientConnection::StatusCode::kSuccess, status);
    sync_completion_signal(&connect_completion);
  };

  // Now connect so that we can successfully disconnect
  ASSERT_OK(connection.Connect(&kMinimumConnectReq, std::move(on_connect)));
  ASSERT_OK(sync_completion_wait(&connect_completion, ZX_TIME_INFINITE));

  ASSERT_OK(connection.Disconnect(kStaAddr, kReasonCode, [&](wlan::nxpfmac::IoctlStatus status) {
    EXPECT_EQ(wlan::nxpfmac::IoctlStatus::Failure, status);
  }));
}

TEST_F(ClientConnectionTest, DisconnectWhileDisconnectInProgress) {
  // Test that an attempt to disconnect is refused while another disconnect is in progress.

  constexpr uint8_t kStaAddr[] = {0x0e, 0x0d, 0x16, 0x28, 0x3a, 0x4c};
  constexpr uint16_t kReasonCode = 2;

  pmlan_ioctl_req disconnect_request = nullptr;
  sync_completion_t disconnect_request_received;
  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the connect call. Complete it asynchronously.
        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
        return MLAN_STATUS_PENDING;
      }
      if (bss->sub_command == MLAN_OID_BSS_STOP) {
        // This is the disconnect call. Fail it asynchronously. Don't complete it, leave it hanging
        // so the second disconnect attempt fails because another one is in progress.
        disconnect_request = req;
        sync_completion_signal(&disconnect_request_received);
        return MLAN_STATUS_PENDING;
      }
    }
    return HandleOtherIoctls(req);
  };
  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  ClientConnection connection(&test_ifc_, &context_, key_ring_.get(), kTestBssIndex);

  sync_completion_t connect_completion;
  auto on_connect = [&](ClientConnection::StatusCode status, const uint8_t *ies, size_t ies_len) {
    EXPECT_EQ(ClientConnection::StatusCode::kSuccess, status);
    sync_completion_signal(&connect_completion);
  };

  // Now connect so that we can successfully disconnect
  ASSERT_OK(connection.Connect(&kMinimumConnectReq, std::move(on_connect)));
  ASSERT_OK(sync_completion_wait(&connect_completion, ZX_TIME_INFINITE));

  ASSERT_OK(connection.Disconnect(kStaAddr, kReasonCode, [&](wlan::nxpfmac::IoctlStatus status) {
    EXPECT_EQ(wlan::nxpfmac::IoctlStatus::Success, status);
  }));

  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, connection.Disconnect(kStaAddr, kReasonCode, [](auto) {}));

  // Now make sure that the first disconnect request was received, then complete it so the
  // connection can be destroyed.
  sync_completion_wait(&disconnect_request_received, ZX_TIME_INFINITE);
  ioctl_adapter_->OnIoctlComplete(disconnect_request, wlan::nxpfmac::IoctlStatus::Success);
}

}  // namespace
