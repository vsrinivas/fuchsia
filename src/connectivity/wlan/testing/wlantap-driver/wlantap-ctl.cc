// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fidl/fuchsia.wlan.tap/cpp/fidl.h>
#include <fidl/fuchsia.wlan.tap/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <memory>
#include <mutex>

#include <ddktl/fidl.h>
#include <src/lib/fidl/cpp/include/lib/fidl/cpp/wire_natural_conversions.h>

#include "src/connectivity/wlan/testing/wlantap-driver/wlantapctl_bind.h"
#include "wlantap-phy.h"

namespace {

namespace wlan_tap = fuchsia_wlan_tap::wire;

// Max size of WlantapPhyConfig.
constexpr size_t kWlantapPhyConfigBufferSize =
    fidl::MaxSizeInChannel<wlan_tap::WlantapPhyConfig, fidl::MessageDirection::kSending>();

static fidl::Arena<kWlantapPhyConfigBufferSize> phy_config_arena;

class WlantapDriver {
 public:
  zx_status_t GetOrStartLoop(async_dispatcher_t** out) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!loop_) {
      auto l = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
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

struct WlantapCtl : fidl::WireServer<fuchsia_wlan_tap::WlantapCtl> {
  WlantapCtl(WlantapDriver* driver) : driver_(driver) {}

  static void DdkRelease(void* ctx) { delete static_cast<WlantapCtl*>(ctx); }

  void CreatePhy(CreatePhyRequestView request, CreatePhyCompleter::Sync& completer) override {
    zx_status_t status;
    phy_config_arena.Reset();

    async_dispatcher_t* loop;
    if ((status = driver_->GetOrStartLoop(&loop)) != ZX_OK) {
      completer.Reply(status);
      return;
    }
    auto natural_config = fidl::ToNatural(request->config);
    auto wire_config = fidl::ToWire(phy_config_arena, std::move(natural_config));
    auto phy_config = std::make_shared<wlan_tap::WlantapPhyConfig>(wire_config);

    if ((status = wlan::CreatePhy(device_, request->proxy.TakeChannel(), phy_config, loop)) !=
        ZX_OK) {
      completer.Reply(status);
    } else {
      completer.Reply(ZX_OK);
    }
  }

  static zx_status_t DdkMessage(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    auto self = static_cast<WlantapCtl*>(ctx);

    DdkTransaction transaction(txn);

    fidl::WireDispatch<fuchsia_wlan_tap::WlantapCtl>(
        self, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
    return transaction.Status();
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
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
    return status;
  }
  // Transfer ownership to devmgr
  wlantapctl.release();
  return ZX_OK;
}

void wlantapctl_release(void* ctx) { delete static_cast<WlantapDriver*>(ctx); }

static constexpr zx_driver_ops_t wlantapctl_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.init = wlantapctl_init;
  ops.bind = wlantapctl_bind;
  ops.release = wlantapctl_release;
  return ops;
}();

ZIRCON_DRIVER(wlantapctl, wlantapctl_driver_ops, "fuchsia", "0.1");
