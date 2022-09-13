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

#include <lib/ddk/debug.h>
#include <netinet/if_ether.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"

namespace wlan::nxpfmac {

constexpr zx_duration_t kConnectionTimeout = ZX_MSEC(6000);

ClientConnection::ClientConnection(IoctlAdapter* ioctl_adapter, uint32_t bss_index)
    : ioctl_adapter_(ioctl_adapter),
      bss_index_(bss_index),
      connect_request_(new IoctlRequest<mlan_ds_bss>()) {}

ClientConnection::~ClientConnection() {
  // Cancel any ongoing connection attempt.
  zx_status_t status = CancelConnect();
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    NXPF_ERR("Failed to cancel connection: %s", zx_status_get_string(status));
    // Don't attempt to wait for the correct state here, it might never happen.
    return;
  }
  // Wait until any connection attempt has completed. A connection attempt will cause asynchronous
  // callbacks that could crash if the connection object goes away. By waiting for an attempt to
  // finish we avoid this. The CancelConnect call above should immediately try to stop any ongoing
  // connection attempt.
  std::unique_lock lock(mutex_);
  connect_in_progress_.Wait(lock, false);
}

zx_status_t ClientConnection::Connect(const uint8_t (&bssid)[ETH_ALEN], uint8_t channel,
                                      OnConnectCallback&& on_connect) {
  std::lock_guard lock(mutex_);
  if (connect_in_progress_) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  on_connect_ = std::move(on_connect);

  auto on_connect_complete = [this](mlan_ioctl_req* req, IoctlStatus io_status) {
    std::lock_guard lock(mutex_);
    if (!connect_in_progress_) {
      NXPF_WARN("Connection ioctl completed when no connection was in progress");
      return;
    }

    if (io_status == IoctlStatus::Timeout) {
      NXPF_WARN("Connection attempt timed out");
      CompleteConnection(StatusCode::kRefusedReasonUnspecified);
      return;
    }
    if (io_status == IoctlStatus::Canceled) {
      CompleteConnection(StatusCode::kCanceled);
      return;
    }

    auto& request = reinterpret_cast<IoctlRequest<mlan_ds_bss>*>(req)->UserReq();
    const mlan_ds_misc_assoc_rsp& assoc_rsp = request.param.ssid_bssid.assoc_rsp;

    StatusCode status_code = StatusCode::kSuccess;
    if (assoc_rsp.assoc_resp_len >= sizeof(IEEEtypes_AssocRsp_t)) {
      auto response = reinterpret_cast<const IEEEtypes_AssocRsp_t*>(assoc_rsp.assoc_resp_buf);
      status_code = static_cast<StatusCode>(response->status_code);
    } else if (io_status != IoctlStatus::Success) {
      status_code = StatusCode::kJoinFailure;
    }

    CompleteConnection(status_code);
  };

  *connect_request_ = IoctlRequest<mlan_ds_bss>(
      MLAN_IOCTL_BSS, MLAN_ACT_SET, bss_index_,
      mlan_ds_bss{
          .sub_command = MLAN_OID_BSS_START,
          .param = {.ssid_bssid = {.idx = bss_index_, .channel = channel}},
      });
  memcpy(connect_request_->UserReq().param.ssid_bssid.bssid, bssid, ETH_ALEN);

  // This should be set before issuing the ioctl. The ioctl completion could theoretically be called
  // before IssueIoctl even returns.
  connect_in_progress_ = true;

  IoctlStatus io_status = ioctl_adapter_->IssueIoctl(
      connect_request_.get(), std::move(on_connect_complete), kConnectionTimeout);
  if (io_status != IoctlStatus::Pending) {
    // Even IoctlStatus::Success should  be considered a failure here. Connecting has to be a
    // pending operation, anything else is unreasonable.
    NXPF_ERR("Connect ioctl failed: %d", io_status);
    connect_in_progress_ = false;
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t ClientConnection::CancelConnect() {
  std::lock_guard lock(mutex_);

  if (!connect_in_progress_) {
    // No connection in progress
    return ZX_ERR_NOT_FOUND;
  }

  ioctl_adapter_->CancelIoctl(connect_request_.get());

  return ZX_OK;
}

zx_status_t ClientConnection::Disconnect() {
  std::lock_guard lock(mutex_);
  if (!connected_) {
    return ZX_ERR_NOT_CONNECTED;
  }

  // Maybe one day..
  return ZX_ERR_NOT_SUPPORTED;
}

void ClientConnection::TriggerConnectCallback(StatusCode status_code) {
  if (on_connect_) {
    on_connect_(status_code);
    // Clear out the callback after using it.
    on_connect_ = OnConnectCallback();
  }
}

void ClientConnection::CompleteConnection(StatusCode status_code) {
  if (!connect_in_progress_) {
    NXPF_WARN("Received connection completion with no connection attempt in progress, ignoring.");
    return;
  }
  connected_ = status_code == StatusCode::kSuccess;
  connect_in_progress_ = false;

  TriggerConnectCallback(status_code);
}
}  // namespace wlan::nxpfmac
