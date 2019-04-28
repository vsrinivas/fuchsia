// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/tap/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <wlan/common/channel.h>
#include <wlan/protocol/ioctl.h>
#include <wlan/protocol/wlantap.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <memory>
#include <mutex>

#include "wlantap-phy.h"

namespace {

namespace wlantap = ::fuchsia::wlan::tap;

class WlantapDriver {
 public:
  zx_status_t GetOrStartLoop(async_dispatcher_t** out) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!loop_) {
      auto l = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
      zx_status_t status = l->StartThread("wlantap-loop");
      if (status != ZX_OK) {
        return status;
      }
      loop_ = std::move(l);
    }
    *out = loop_->dispatcher();
    return ZX_OK;
  }

 private:
  std::mutex mutex_;
  std::unique_ptr<async::Loop> loop_;
};

zx_status_t SendCreatePhyResponse(zx_txid_t txid, zx_status_t status,
                                  fidl_txn_t* txn) {
  fuchsia_wlan_tap_WlantapCtlCreatePhyResponse resp{
      .hdr =
          {
              .ordinal = fuchsia_wlan_tap_WlantapCtlCreatePhyOrdinal,
              .txid = txid,
          },
      .status = status,
  };
  fidl_msg_t resp_msg = {.bytes = &resp,
                         .num_bytes = sizeof(resp),
                         .handles = nullptr,
                         .num_handles = 0};
  zx_status_t st = txn->reply(txn, &resp_msg);
  if (st != ZX_OK) {
    zxlogf(ERROR, "error sending response: %s\n", zx_status_get_string(st));
    return st;
  }
  return status;
}

zx_status_t DecodeCreatePhyRequest(
    fidl_msg_t* msg, fidl_txn_t* txn,
    fidl::InterfaceRequest<wlantap::WlantapPhy>* out_req,
    wlantap::WlantapPhyConfig* out_config, zx_txid_t* out_txid) {
  zx_status_t status = ZX_OK;
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zxlogf(ERROR, "wlantapctl: CreatePhyRequest too short for header\n");
    return ZX_ERR_INVALID_ARGS;
  }
  auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);
  *out_txid = hdr->txid;
  if (hdr->ordinal != fuchsia_wlan_tap_WlantapCtlCreatePhyOrdinal) {
    zxlogf(ERROR, "wlantapctl: ordinal not supported, expecting %u, got %u\n",
           fuchsia_wlan_tap_WlantapCtlCreatePhyOrdinal, hdr->ordinal);
    return ZX_ERR_NOT_SUPPORTED;
  }

  const char* error_msg = nullptr;
  status = fidl_decode_msg(&fuchsia_wlan_tap_WlantapCtlCreatePhyRequestTable,
                           msg, &error_msg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "wlantapctl: cannot decode CreatePhy request: %s - %s\n",
           zx_status_get_string(status), error_msg);
    return status;
  }

  fidl::Message fidl_msg(fidl::BytePart(static_cast<uint8_t*>(msg->bytes),
                                        msg->num_bytes, msg->num_bytes),
                         fidl::HandlePart(msg->handles, msg->num_handles));
  fidl::Decoder decoder(std::move(fidl_msg));
  *out_config = fidl::DecodeAs<wlantap::WlantapPhyConfig>(
      &decoder, sizeof(fidl_message_header_t));
  *out_req = fidl::DecodeAs<fidl::InterfaceRequest<wlantap::WlantapPhy>>(
      &decoder, sizeof(fidl_message_header_t) +
                    sizeof(fuchsia_wlan_tap_WlantapPhyConfig));
  return ZX_OK;
}

struct WlantapCtl {
  WlantapCtl(WlantapDriver* driver) : driver_(driver) {}

  static void DdkRelease(void* ctx) { delete static_cast<WlantapCtl*>(ctx); }

  static zx_status_t DdkMessage(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    fidl::InterfaceRequest<wlantap::WlantapPhy> request;
    wlantap::WlantapPhyConfig config;
    zx_txid_t txid;
    zx_status_t status =
        DecodeCreatePhyRequest(msg, txn, &request, &config, &txid);
    if (status != ZX_OK) {
      return SendCreatePhyResponse(txid, status, txn);
    }

    async_dispatcher_t* loop;
    auto& self = *static_cast<WlantapCtl*>(ctx);

    status = self.driver_->GetOrStartLoop(&loop);
    if (status != ZX_OK) {
      zxlogf(ERROR, "wlantapctl: could not start wlantap event loop: %s\n",
             zx_status_get_string(status));
      return SendCreatePhyResponse(txid, status, txn);
    }

    auto phy_config =
        std::make_unique<wlantap::WlantapPhyConfig>(std::move(config));
    auto channel = request.TakeChannel();
    status = wlan::CreatePhy(self.device_, std::move(channel),
                             std::move(phy_config), loop);
    if (status != ZX_OK) {
      zxlogf(ERROR, "wlantapctl: could not create phy: %s\n",
             zx_status_get_string(status));
    }
    return SendCreatePhyResponse(txid, status, txn);
  }

  zx_device_t* device_ = nullptr;
  WlantapDriver* driver_;
};

}  // namespace

zx_status_t wlantapctl_init(void** out_ctx) {
  *out_ctx = new WlantapDriver();
  return ZX_OK;
}

zx_status_t wlantapctl_bind(void* ctx, zx_device_t* parent) {
  auto driver = static_cast<WlantapDriver*>(ctx);
  auto wlantapctl = std::make_unique<WlantapCtl>(driver);
  static zx_protocol_device_t device_ops = {
      .version = DEVICE_OPS_VERSION,
      .release = &WlantapCtl::DdkRelease,
      .message = &WlantapCtl::DdkMessage,
  };
  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = "wlantapctl",
                            .ctx = wlantapctl.get(),
                            .ops = &device_ops};
  zx_status_t status = device_add(parent, &args, &wlantapctl->device_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d\n", __func__, status);
    return status;
  }
  // Transfer ownership to devmgr
  wlantapctl.release();
  return ZX_OK;
}

void wlantapctl_release(void* ctx) { delete static_cast<WlantapDriver*>(ctx); }

static zx_driver_ops_t wlantapctl_driver_ops = []() {
  zx_driver_ops_t ops;
  ops.version = DRIVER_OPS_VERSION;
  ops.init = wlantapctl_init;
  ops.bind = wlantapctl_bind;
  ops.release = wlantapctl_release;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(wlantapctl, wlantapctl_driver_ops, "fuchsia", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(wlantapctl)
