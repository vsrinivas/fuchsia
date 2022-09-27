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

#include <lib/sync/completion.h>
#include <netinet/ether.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/key_ring.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"

namespace {

using wlan::nxpfmac::ClientConnection;
using wlan::nxpfmac::ClientConnectionIfc;

constexpr uint8_t kIesWithSsid[] = {"\x00\x04Test"};
constexpr uint8_t kTestChannel = 6;
constexpr uint32_t kTestBssIndex = 3;
constexpr wlan_fullmac_connect_req_t kMinimumConnectReq = {
    .selected_bss{.ies_list = kIesWithSsid,
                  .ies_count = sizeof(kIesWithSsid),
                  .channel{.primary = kTestChannel}},
    .auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM};

class TestClientConnectionIfc : public ClientConnectionIfc {
  void OnDisconnectEvent(uint16_t reason_code) override {}
};

struct ClientConnectionTest : public zxtest::Test {
  void SetUp() override {
    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    key_ring_ = std::make_unique<wlan::nxpfmac::KeyRing>(ioctl_adapter_.get(), kTestBssIndex);
    context_ = wlan::nxpfmac::DeviceContext{.event_handler_ = &event_handler_,
                                            .ioctl_adapter_ = ioctl_adapter_.get()};
  }

  wlan::nxpfmac::MlanMockAdapter mocks_;
  wlan::nxpfmac::MockBus mock_bus_;
  wlan::nxpfmac::EventHandler event_handler_;
  wlan::nxpfmac::DeviceContext context_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
  std::unique_ptr<wlan::nxpfmac::KeyRing> key_ring_;
  TestClientConnectionIfc test_ifc_;
};

TEST_F(ClientConnectionTest, Constructible) {
  ASSERT_NO_FATAL_FAILURE(ClientConnection(&test_ifc_, &context_, nullptr, 0));
}

TEST_F(ClientConnectionTest, Connect) {
  constexpr uint8_t kBss[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  constexpr uint32_t kBssIndex = 0;

  wlan_fullmac_connect_req_t request = kMinimumConnectReq;
  memcpy(request.selected_bss.bssid, kBss, sizeof(kBss));

  sync_completion_t ioctl_completion;

  auto on_ioctl = [&](t_void *, pmlan_ioctl_req req) -> mlan_status {
    if (req->action == MLAN_ACT_SET && req->req_id == MLAN_IOCTL_BSS) {
      auto bss = reinterpret_cast<const mlan_ds_bss *>(req->pbuf);
      if (bss->sub_command == MLAN_OID_BSS_START) {
        // This is the actual connect ioctl.

        auto request = reinterpret_cast<wlan::nxpfmac::IoctlRequest<mlan_ds_bss> *>(req);
        auto &user_req = request->UserReq();
        EXPECT_EQ(MLAN_IOCTL_BSS, request->IoctlReq().req_id);
        EXPECT_EQ(MLAN_ACT_SET, request->IoctlReq().action);

        EXPECT_EQ(MLAN_OID_BSS_START, user_req.sub_command);
        EXPECT_EQ(kBssIndex, user_req.param.ssid_bssid.idx);
        EXPECT_EQ(kTestChannel, user_req.param.ssid_bssid.channel);
        EXPECT_BYTES_EQ(kBss, user_req.param.ssid_bssid.bssid, ETH_ALEN);

        ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);

        sync_completion_signal(&ioctl_completion);

        return MLAN_STATUS_PENDING;
      }
    }
    // Other ioctl can just complete immediately.
    return MLAN_STATUS_SUCCESS;
  };

  mocks_.SetOnMlanIoctl(std::move(on_ioctl));

  ClientConnection connection(&test_ifc_, &context_, key_ring_.get(), kBssIndex);

  sync_completion_t connect_completion;
  auto on_connect = [&](ClientConnection::StatusCode status_code, const uint8_t *ies,
                        size_t ies_size) {
    EXPECT_EQ(ClientConnection::StatusCode::kSuccess, status_code);
    sync_completion_signal(&connect_completion);
  };

  ASSERT_OK(connection.Connect(&request, std::move(on_connect)));

  ASSERT_OK(sync_completion_wait(&ioctl_completion, ZX_TIME_INFINITE));
  ASSERT_OK(sync_completion_wait(&connect_completion, ZX_TIME_INFINITE));
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
    return MLAN_STATUS_SUCCESS;
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
    if (req->req_id == MLAN_IOCTL_SEC_CFG || req->req_id == MLAN_IOCTL_MISC_CFG) {
      // Connecting performs security and IE (MISC ioctl) configuration, make sure it succeeds.
      return MLAN_STATUS_SUCCESS;
    }

    // Unexpected
    ADD_FAILURE("Should not reach this point, unexpected ioctl");
    return MLAN_STATUS_FAILURE;
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
    if (req->req_id == MLAN_IOCTL_SEC_CFG || req->req_id == MLAN_IOCTL_MISC_CFG) {
      // Connecting performs security and IE (MISC ioctl) configuration, make sure it succeeds.
      return MLAN_STATUS_SUCCESS;
    }

    // Unexpected
    ADD_FAILURE("Should not reach this point, unexpected ioctl");
    return MLAN_STATUS_FAILURE;
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
    if (req->req_id == MLAN_IOCTL_SEC_CFG || req->req_id == MLAN_IOCTL_MISC_CFG) {
      // Connecting performs security and IE (MISC ioctl) configuration, make sure it succeeds.
      return MLAN_STATUS_SUCCESS;
    }

    // Unexpected
    ADD_FAILURE("Should not reach this point, unexpected ioctl");
    return MLAN_STATUS_FAILURE;
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
