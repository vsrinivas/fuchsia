// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <wlan/protocol/ioctl.h>
#include <wlan/protocol/wlantap.h>

#include <wlan/common/channel.h>
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
            if (status != ZX_OK) { return status; }
            loop_ = std::move(l);
        }
        *out = loop_->dispatcher();
        return ZX_OK;
    }

   private:
    std::mutex mutex_;
    std::unique_ptr<async::Loop> loop_;
};

template <typename T> zx_status_t DecodeFidl(const void* data, size_t size, T* out) {
    std::vector<uint8_t> decoded(size);
    memcpy(&decoded[0], data, size);
    const char* err_msg = nullptr;
    fidl::Message msg(fidl::BytePart(decoded.data(), size, size), fidl::HandlePart());
    if (msg.Decode(T::FidlType, &err_msg) != ZX_OK) {
        zxlogf(ERROR, "failed to decode FIDL message: %s\n", err_msg);
        return ZX_ERR_INVALID_ARGS;
    }
    fidl::Decoder dec(std::move(msg));
    T::Decode(&dec, out, 0);
    return ZX_OK;
}

struct WlantapCtl {
    WlantapCtl(WlantapDriver* driver) : driver_(driver) {}

    static void DdkRelease(void* ctx) { delete static_cast<WlantapCtl*>(ctx); }

    static zx_status_t DdkIoctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len, size_t* out_actual) {
        auto& self = *static_cast<WlantapCtl*>(ctx);
        switch (op) {
        case IOCTL_WLANTAP_CREATE_WLANPHY:
            zxlogf(INFO, "wlantapctl: IOCTL_WLANTAP_CREATE_WLANPHY\n");
            return self.IoctlCreateWlanphy(in_buf, in_len, out_buf, out_len, out_actual);
        default:
            zxlogf(ERROR, "wlantapctl: unknown ioctl %u\n", op);
            return ZX_ERR_NOT_SUPPORTED;
        }
    }

    zx_status_t IoctlCreateWlanphy(const void* in_buf, size_t in_len, void* out_buf, size_t out_len,
                                   size_t* out_actual) {
        if (in_buf == nullptr || in_len < sizeof(wlantap_ioctl_create_wlanphy_t)) {
            zxlogf(ERROR, "wlantapctl: IOCTL_WLANTAP_CREATE_WLANPHY: invalid input buffer\n");
            return ZX_ERR_INVALID_ARGS;
        }
        auto& in = *static_cast<const wlantap_ioctl_create_wlanphy_t*>(in_buf);
        // Immediately wrap the handle to make sure we don't leak it
        zx::channel user_channel(in.channel);

        auto phy_config = wlantap::WlantapPhyConfig::New();
        const uint8_t* in_end = static_cast<const uint8_t*>(in_buf) + in_len;
        zx_status_t status = DecodeFidl(&in.config[0], in_end - &in.config[0], phy_config.get());
        if (status != ZX_OK) {
            zxlogf(
                ERROR,
                "wlantapctl: IOCTL_WLANTAP_CREATE_WLANPHY: failed to parse input buffer as FIDL\n");
            return status;
        }

        async_dispatcher_t* loop;
        status = driver_->GetOrStartLoop(&loop);
        if (status != ZX_OK) {
            zxlogf(ERROR, "wlantapctl: could not start wlantap event loop: %d\n", status);
            return status;
        }
        status = wlan::CreatePhy(device_, std::move(user_channel), std::move(phy_config), loop);
        if (status != ZX_OK) {
            zxlogf(ERROR, "wlantapctl: could not create wlantap phy: %d\n", status);
            return status;
        }
        if (out_actual != nullptr) { *out_actual = 0; }
        zxlogf(ERROR, "wlantapctl: IOCTL_WLANTAP_CREATE_WLANPHY: success\n");
        return ZX_OK;
    }

    zx_device_t* device_ = nullptr;
    WlantapDriver* driver_;
};

}  // namespace

extern "C" zx_status_t wlantapctl_init(void** out_ctx) {
    *out_ctx = new WlantapDriver();
    return ZX_OK;
}

extern "C" zx_status_t wlantapctl_bind(void* ctx, zx_device_t* parent) {
    auto driver = static_cast<WlantapDriver*>(ctx);
    auto wlantapctl = std::make_unique<WlantapCtl>(driver);
    static zx_protocol_device_t device_ops = {
        .version = DEVICE_OPS_VERSION,
        .release = &WlantapCtl::DdkRelease,
        .ioctl = &WlantapCtl::DdkIoctl,
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

extern "C" void wlantapctl_release(void* ctx) {
    delete static_cast<WlantapDriver*>(ctx);
}
