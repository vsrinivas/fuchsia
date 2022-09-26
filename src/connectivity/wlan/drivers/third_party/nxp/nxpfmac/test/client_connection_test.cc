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
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/key_ring.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"

namespace {

using wlan::nxpfmac::ClientConnection;

constexpr uint8_t kIesWithSsid[] = {"\x00\x04Test"};
constexpr uint8_t kTestChannel = 6;
constexpr uint32_t kTestBssIndex = 3;
constexpr wlan_fullmac_connect_req_t kMinimumConnectReq = {
    .selected_bss{.ies_list = kIesWithSsid,
                  .ies_count = sizeof(kIesWithSsid),
                  .channel{.primary = kTestChannel}},
    .auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM};

struct ClientConnectionTest : public zxtest::Test {
  void SetUp() override {
    auto ioctl_adapter = wlan::nxpfmac::IoctlAdapter::Create(mocks_.GetAdapter(), &mock_bus_);
    ASSERT_OK(ioctl_adapter.status_value());
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    key_ring_ = std::make_unique<wlan::nxpfmac::KeyRing>(ioctl_adapter_.get(), kTestBssIndex);
  }

  wlan::nxpfmac::MlanMockAdapter mocks_;
  wlan::nxpfmac::MockBus mock_bus_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
  std::unique_ptr<wlan::nxpfmac::KeyRing> key_ring_;
};

TEST(ConnectionTest, Constructible) {
  ASSERT_NO_FATAL_FAILURE(ClientConnection(nullptr, nullptr, 0));
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

  ClientConnection connection(ioctl_adapter_.get(), key_ring_.get(), kBssIndex);

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

  ClientConnection connection(ioctl_adapter_.get(), key_ring_.get(), kBssIndex);

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

}  // namespace
