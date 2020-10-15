// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/device/llcpp/fidl.h>
#include <fuchsia/wlan/tap/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <memory>
#include <mutex>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddktl/fidl.h>

#include "wlantap-phy.h"

namespace {

namespace wlantap = ::llcpp::fuchsia::wlan::tap;

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

struct WlantapCtl : wlantap::WlantapCtl::Interface {
  WlantapCtl(WlantapDriver* driver) : driver_(driver) {}

  static void DdkRelease(void* ctx) { delete static_cast<WlantapCtl*>(ctx); }

  void CreatePhy(wlantap::WlantapPhyConfig config, ::zx::channel proxy,
                 CreatePhyCompleter::Sync& completer) override {
    zx_status_t status;

    async_dispatcher_t* loop;
    if ((status = driver_->GetOrStartLoop(&loop)) != ZX_OK) {
      completer.Reply(status);
      return;
    }

    // Convert to HLCPP by transiting through fidl bytes.
    auto phy_config = ::fuchsia::wlan::tap::WlantapPhyConfig::New();
    {
      fidl::Buffer<wlantap::WlantapPhyConfig> buffer;
      auto encode_result = fidl::LinearizeAndEncode(&config, buffer.view());
      if ((status = encode_result.status) != ZX_OK) {
        completer.Reply(status);
        return;
      }
      auto decode_result = fidl::Decode(std::move(encode_result.message));
      if ((status = decode_result.status) != ZX_OK) {
        completer.Reply(status);
        return;
      }
      fidl::Decoder dec(fidl::Message(decode_result.message.Release(), fidl::HandlePart()));
      ::fuchsia::wlan::tap::WlantapPhyConfig::Decode(&dec, phy_config.get(), /* offset = */ 0);
    }

    if ((status = wlan::CreatePhy(device_, std::move(proxy), std::move(phy_config), loop)) !=
        ZX_OK) {
      completer.Reply(status);
    } else {
      completer.Reply(ZX_OK);
    }
  };

  static zx_status_t DdkMessage(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    auto self = static_cast<WlantapCtl*>(ctx);

    DdkTransaction transaction(txn);
    wlantap::WlantapCtl::Dispatch(self, msg, &transaction);
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

// clang-format off
ZIRCON_DRIVER_BEGIN(wlantapctl, wlantapctl_driver_ops, "fuchsia", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(wlantapctl)
